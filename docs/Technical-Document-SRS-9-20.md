# Technical Document — Counter-UAS Radar Tracker  
**Based on Technical Specification: SRS-9-20**

**Document version:** 1.0  
**Target stack:** Qt, Windows/Linux, Python (data analytics)  
**Last updated:** February 2026

---

## 1. Introduction

### 1.1 Purpose

This technical document describes the **technical design and implementation approach** for the Counter-UAS Radar Tracker application. It is prepared in alignment with the technical specification **SRS-9-20** and defines the chosen technology stack, architecture, components, and integration strategy for development on **Windows and Linux**, using **Qt** for the application layer and **Python** for deeper data analytics.

### 1.2 Scope

- **In scope:** Core tracker pipeline (receiver → preprocessing → clustering → prediction → association → track management → sender), Qt-based application shell (configuration, monitoring, optional UI), cross-platform build and deployment (Windows/Linux), and Python-based analytics tooling.
- **Out of scope:** Radar hardware or DSP subsystem design; detailed SRS requirement traceability (covered by the SRS-9-20 specification itself).

### 1.3 Reference Documents

| Document | Description |
|----------|-------------|
| **SRS-9-20** | Technical specification (source requirements) for the Counter-UAS Radar Tracker system. |
| **CounterUAS_RadarTracker_TDD.pptx** | Technical Design Deck (system context, components, UML, sequences, track lifecycle). |
| **CMakeLists.txt** | Build definition and library/executable targets. |
| **tracker_config.json** | Runtime configuration (system, network, preprocessing, clustering, prediction, association, track management, display). |

### 1.4 Definitions and Abbreviations

| Term | Definition |
|------|------------|
| **CUAS** | Counter-Unmanned Aircraft System. |
| **DSP** | Digital Signal Processing (upstream detection source). |
| **UAV** | Unmanned Aerial Vehicle. |
| **IMM** | Interacting Multiple Model (filter). |
| **CV/CA/CTR** | Constant Velocity / Constant Acceleration / Coordinated Turn (motion models). |
| **GNN** | Global Nearest Neighbor (association). |
| **JPDA** | Joint Probabilistic Data Association. |
| **DBSCAN** | Density-Based Spatial Clustering of Applications with Noise. |

---

## 2. Technology Stack

### 2.1 Overview

The application is developed for **Windows and Linux** using the following stack:

| Layer | Technology | Role |
|-------|------------|------|
| **Application / UI** | **Qt** (C++) | Cross-platform GUI (if required), configuration UI, monitoring dashboards, process lifecycle. |
| **Core tracker** | **C++17** (existing codebase) | Real-time pipeline: receiver, preprocessing, clustering, prediction, association, track management, sender. |
| **Build system** | **CMake 3.14+** | Single build definition for Windows (MSVC) and Linux (GCC/Clang). |
| **Data analytics** | **Python 3** | Offline and optional online analytics: performance metrics, track statistics, clustering/prediction analysis, visualization. |

### 2.2 Qt (Windows & Linux)

- **Role:** Provide a consistent application framework across Windows and Linux (windowing, networking, threading, configuration, optional GUI).
- **Use cases:** Launcher/controller for the tracker process, configuration editor, real-time status/monitoring, future operator displays.
- **Versions:** Qt 5.15+ or Qt 6.x; minimum version to be fixed per project policy.
- **Modules (typical):** Core, GUI (if UI needed), Widgets, Network, Concurrent.

### 2.3 C++ Core Tracker

- **Standard:** C++17.
- **Platform:** Windows (MSVC; `ws2_32` for sockets), Linux (GCC/Clang; `pthread`).
- **Libraries (from CMake):**  
  `cuas_common`, `cuas_receiver`, `cuas_preprocessing`, `cuas_clustering`, `cuas_prediction`, `cuas_association`, `cuas_track_management`, `cuas_sender`, `cuas_pipeline`.
- **Executable:** `cuas_tracker` (main pipeline); can be driven by a Qt host process or run standalone.

### 2.4 Python for Data Analytics

- **Role:** Deeper analysis of tracker outputs (logs, exported `.dat` files, or live feeds if bridged).
- **Typical libraries:**  
  NumPy, SciPy, Pandas, Matplotlib/Seaborn, scikit-learn (e.g. clustering validation, prediction error analysis), Jupyter for notebooks.
- **Data sources:**  
  `logs/`, `exportedData*/` (e.g. `raw_detections.dat`, `preprocessed.dat`, `clusters.dat`, `predictions.dat`, `*_track_*.dat`).
- **Deliverables:** Scripts, notebooks, and (optionally) a small Python service/API consumed by the Qt application for embedded analytics views.

---

## 3. System Architecture

### 3.1 High-Level Context

```
  [DSP / Radar]  ----UDP---->  [Tracker]  ----UDP---->  [Display / C2]
                                    |
                                    +---- (optional) ----> [Python analytics]
```

- **DSP:** Sends detection messages (dwells) to the tracker (configurable IP/port, e.g. `receiverPort: 50000`).
- **Tracker:** Receives detections, runs preprocessing → clustering → prediction → association → track management, sends track updates to the display (e.g. `senderPort: 50001`).
- **Display/C2:** Consumes track messages (and optionally raw detections) for operator display.
- **Python analytics:** Consumes logs and/or exported data for post-processing and deeper analysis.

### 3.2 Component View (Core + Qt + Python)

| Component | Implementation | Responsibility |
|-----------|----------------|----------------|
| **Detection receiver** | C++ (`cuas_receiver`) | UDP receive, parsing, queueing to pipeline. |
| **Preprocessing** | C++ (`cuas_preprocessing`) | Range/azimuth/elevation/SNR/RCS/strength gating. |
| **Clustering** | C++ (`cuas_clustering`) | DBSCAN, range-based, or range-strength clustering. |
| **Prediction** | C++ (`cuas_prediction`) | CV, CA, CTR models; IMM. |
| **Association** | C++ (`cuas_association`) | Mahalanobis, GNN, or JPDA. |
| **Track management** | C++ (`cuas_track_management`) | Initiation (e.g. m-of-n), maintenance, deletion, quality. |
| **Track sender** | C++ (`cuas_sender`) | UDP send to display. |
| **Pipeline** | C++ (`cuas_pipeline`) | Orchestration, cycle timing, logging, optional export. |
| **Qt application** | Qt (C++) | Process control, config load/save, UI (if any), monitoring. |
| **Python analytics** | Python 3 | Scripts/notebooks and optional analytics service. |

### 3.3 Data Flow (Per Dwell)

1. **Receive** detection message (UDP).
2. **Preprocess** (gate by range, azimuth, elevation, SNR, RCS, strength).
3. **Cluster** detections (configurable method).
4. **Predict** existing tracks (IMM with CV/CA/CTR).
5. **Associate** clusters to tracks (configurable method).
6. **Update** track state (initiation, update, coasting, deletion).
7. **Send** track output to display (and optionally raw/cluster/track exports for analytics).

---

## 4. Qt Application Integration

### 4.1 Responsibilities

- **Process lifecycle:** Start/stop the tracker process (or load tracker as a library if architecture allows).
- **Configuration:** Load/save/edit `tracker_config.json` (paths, network, preprocessing, clustering, prediction, association, track management, display).
- **Monitoring:** Show status, cycle counts, errors; optional real-time plots (e.g. track count, latency).
- **Logging:** Redirect or expose log directory (`logDirectory`) and log level (`logLevel`) from config.

### 4.2 Platform-Specific Notes

- **Windows:** Use Qt’s process and file APIs; ensure firewall/permissions for UDP ports; standardize install path for `config/` and `logs/`.
- **Linux:** Same Qt APIs; consider systemd or script for headless deployment; `config/` and `logs/` paths via environment or config file.

### 4.3 Build Integration

- Add Qt to CMake (e.g. `find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)` or Qt5 equivalent).
- New targets: e.g. `cuas_qt_launcher` or `cuas_config_ui`, linking Qt and (as needed) the pipeline or calling the `cuas_tracker` executable.

---

## 5. Python Data Analytics

### 5.1 Objectives

- **Performance analysis:** Latency, throughput, track longevity, association metrics.
- **Algorithm tuning:** Clustering parameters, IMM/association thresholds, initiation rules.
- **Visualization:** Track trajectories, dwell-level detections, cluster shapes, prediction errors.
- **Export format analysis:** Parse `.dat` exports (raw_detections, preprocessed, clusters, predictions, tracks) for reproducibility and debugging.

### 5.2 Recommended Libraries

| Library | Purpose |
|--------|--------|
| **NumPy** | Arrays, numerical ops on detection/track data. |
| **Pandas** | Tabular data (tracks, detections over time). |
| **Matplotlib / Seaborn** | Plots, heatmaps, time series. |
| **SciPy** | Statistics, optimization (e.g. threshold tuning). |
| **scikit-learn** | Clustering comparison, metrics (if re-running clustering in Python). |
| **Jupyter** | Notebooks for exploratory analysis and reporting. |

### 5.3 Integration with Tracker

- **Offline:** Python reads `logs/` and `exportedData*/` after runs; no change to C++ tracker.
- **Optional online:** Tracker writes to a socket or file watched by Python; or Qt app spawns Python scripts and displays results (e.g. generated plots).

### 5.4 Dependencies

- Existing docs use `python-pptx`, `pillow` for TDD generation.  
- Analytics: extend `docs/scripts/requirements.txt` (or add `docs/analytics/requirements.txt`) with NumPy, Pandas, Matplotlib, SciPy, scikit-learn, Jupyter as needed.

---

## 6. Build and Deployment

### 6.1 Prerequisites

- **Windows:** Visual Studio (MSVC), CMake 3.14+, Qt (Kit aligned with MSVC), Python 3 for scripts/analytics.
- **Linux:** GCC 8+ or Clang, CMake 3.14+, Qt, Python 3.

### 6.2 Build Steps (Core Tracker)

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

- Install targets: `cuas_tracker`, `dsp_injector`, `display_module`, `log_extractor` to `bin/`; `tracker_config.json` to `config/`; `messages.idl` to `idl/`.

### 6.3 Qt Application Build

- Configure CMake with Qt path (e.g. `-DQt6_DIR=...`).
- Build Qt launcher/UI target; deploy Qt DLLs (Windows) or ensure Qt libs on path (Linux).

### 6.4 Python Environment

```bash
python3 -m venv .venv
.venv\Scripts\activate   # Windows
source .venv/bin/activate # Linux
pip install -r docs/scripts/requirements.txt
# Optional: pip install -r docs/analytics/requirements.txt
```

---

## 7. Configuration

- **Primary config:** `config/tracker_config.json` (copied next to `cuas_tracker` at build time).
- **Sections:** `system`, `network`, `preprocessing`, `clustering`, `prediction`, `association`, `trackManagement`, `display`.
- **Qt application:** May allow editing this file or a user-specific override; tracker (or launcher) loads path from command line or default location.

---

## 8. Interfaces

### 8.1 Network (UDP)

- **Input:** Detections from DSP (receiver IP/port, buffer size in config).
- **Output:** Track (and optionally raw) messages to display (sender IP/port, buffer size in config).
- **IDL:** `idl/messages.idl` defines message formats.

### 8.2 File / Logs

- **Log directory:** `system.logDirectory` (e.g. `./logs`).
- **Exported data:** Optional per-run directories (e.g. `exportedData1/`) with `.dat` files for analytics.

### 8.3 Qt ↔ Tracker

- **Option A:** Qt runs `cuas_tracker` as subprocess; communication via config file and (if needed) stdout/stderr or a small control socket.
- **Option B:** Tracker core linked into Qt executable; direct C++ API for start/stop/config.

---

## 9. Testing and Validation

- **Unit tests:** Add tests for preprocessing, clustering, prediction, association, track logic (platform-agnostic C++).
- **Integration:** Run pipeline with `dsp_injector` and `display_module` simulators; verify track output.
- **Platform:** Build and smoke-test on both Windows and Linux; validate Qt UI on both.
- **Analytics:** Use Python to validate consistency of exported data and to compare algorithm variants (e.g. GNN vs JPDA) against SRS-9-20 requirements where applicable.

---

## 10. Summary

| Aspect | Choice |
|--------|--------|
| **Specification** | SRS-9-20 (technical specification). |
| **Platforms** | Windows, Linux. |
| **Application framework** | Qt (C++). |
| **Core tracker** | C++17 (existing pipeline). |
| **Build** | CMake; Qt optional component. |
| **Data analytics** | Python 3 (NumPy, Pandas, Matplotlib, SciPy, scikit-learn, Jupyter). |
| **Config** | JSON (`tracker_config.json`). |
| **Interfaces** | UDP (DSP → Tracker → Display); file-based logs and exports for Python. |

This technical document should be updated when the SRS-9-20 specification changes or when the Qt/Python integration design is refined. For detailed requirement traceability and rationale, refer to **SRS-9-20** and the project’s Technical Design Deck (TDD).
