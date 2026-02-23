/*
 * Display Module Simulator  —  Multi-Mode Radar Display
 *
 * Receives UDP messages from the tracker and provides selectable views:
 *
 *   [1] Raw Detections   [2] Clusters     [3] Association/Gating
 *   [4] Predicted States [5] All Tracks   [6] Track ID Filter
 *   [7] PPI (top-down)   [8] B-Scope      [9] C-Scope
 *   [0] Time Series
 *
 * Keyboard: press the number/letter key to switch mode.
 * In Track-Filter mode: type a track ID number then Enter.
 *
 * Usage: display_module [listen_port]
 */

#include "common/types.h"
#include "common/udp_socket.h"
#include "common/constants.h"
#include "common/logger.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <atomic>
#include <cmath>
#include <mutex>
#include <cstring>
#include <limits>

#ifdef _WIN32
#  include <conio.h>
#else
#  include <termios.h>
#  include <unistd.h>
#  include <fcntl.h>
#endif

static std::atomic<bool> g_running{true};
void signalHandler(int) { g_running.store(false); }

// ---------------------------------------------------------------------------
// Display mode
// ---------------------------------------------------------------------------
enum class DisplayMode {
    RawDetections = 0,
    Clusters,
    Association,
    Predicted,
    AllTracks,
    TrackFilter,
    PPI,
    BScope,
    CScope,
    TimeSeries
};

static const char* modeName(DisplayMode m) {
    switch (m) {
        case DisplayMode::RawDetections: return "Raw Detections";
        case DisplayMode::Clusters:      return "Clusters";
        case DisplayMode::Association:   return "Association/Gating";
        case DisplayMode::Predicted:     return "Predicted States";
        case DisplayMode::AllTracks:     return "All Tracks";
        case DisplayMode::TrackFilter:   return "Track ID Filter";
        case DisplayMode::PPI:           return "PPI (top-down)";
        case DisplayMode::BScope:        return "B-Scope (Rng vs Az)";
        case DisplayMode::CScope:        return "C-Scope (Az vs El)";
        case DisplayMode::TimeSeries:    return "Time Series";
        default: return "?";
    }
}

// ---------------------------------------------------------------------------
// Per-track history for time series
// ---------------------------------------------------------------------------
static constexpr int HISTORY_LEN = 80;

struct TrackHistory {
    std::deque<double> range, azimuthDeg, elevationDeg, rangeRate;
    std::deque<double> quality;
};

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static DisplayMode                             g_mode            = DisplayMode::AllTracks;
static uint32_t                                g_filterTrackId   = 0;
static bool                                    g_filterInputMode = false;
static std::string                             g_filterInput;

static cuas::SPDetectionMessage                g_latestDets;
static std::vector<cuas::ClusterWire>          g_latestClusters;
static std::vector<cuas::AssocEntryWire>       g_latestAssoc;
static std::vector<cuas::PredictedEntryWire>   g_latestPredicted;
static std::vector<cuas::TrackUpdateMessage>   g_latestTracks;

static uint64_t g_detMsgCount     = 0;
static uint64_t g_clusterMsgCount = 0;
static uint64_t g_assocMsgCount   = 0;
static uint64_t g_predMsgCount    = 0;
static uint64_t g_trackMsgCount   = 0;
static uint32_t g_lastDwellCount  = 0;

static std::map<uint32_t, TrackHistory> g_trackHistory;

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------
static const char* statusStr(cuas::TrackStatus s) {
    switch (s) {
        case cuas::TrackStatus::Tentative: return "TENT";
        case cuas::TrackStatus::Confirmed: return "CONF";
        case cuas::TrackStatus::Coasting:  return "COAST";
        case cuas::TrackStatus::Deleted:   return "DEL";
        default: return "???";
    }
}

static const char* classStr(cuas::TrackClassification c) {
    switch (c) {
        case cuas::TrackClassification::Unknown:        return "UNKNOWN";
        case cuas::TrackClassification::DroneRotary:    return "DRONE-R";
        case cuas::TrackClassification::DroneFixedWing: return "DRONE-F";
        case cuas::TrackClassification::Bird:           return "BIRD";
        case cuas::TrackClassification::Clutter:        return "CLUTTER";
        default: return "???";
    }
}

static std::string nowStr() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

// ---------------------------------------------------------------------------
// Non-blocking keyboard input
// ---------------------------------------------------------------------------
#ifdef _WIN32
static int kbCheck() {
    return _kbhit() ? _getch() : -1;
}
#else
static struct termios g_origTermios;
static void setRawMode() {
    tcgetattr(STDIN_FILENO, &g_origTermios);
    struct termios raw = g_origTermios;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}
static void restoreMode() {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_origTermios);
}
static int kbCheck() {
    char c;
    return (read(STDIN_FILENO, &c, 1) == 1) ? static_cast<int>(c) : -1;
}
#endif

// ---------------------------------------------------------------------------
// ASCII 2D plot helpers
// ---------------------------------------------------------------------------
static constexpr int PLOT_W = 76;
static constexpr int PLOT_H = 20;

using Grid = std::vector<std::string>;

static Grid makeGrid(int w, int h) {
    return Grid(h, std::string(w, ' '));
}

static void plotPoint(Grid& g, int x, int y, char ch) {
    if (x >= 0 && x < static_cast<int>(g[0].size()) && y >= 0 && y < static_cast<int>(g.size()))
        g[y][x] = ch;
}

static void printGrid(const Grid& g) {
    for (const auto& row : g) std::cout << "  |" << row << "|\n";
}

// ---------------------------------------------------------------------------
// ASCII time-series bar chart
// ---------------------------------------------------------------------------
static void printTimeSeries(const std::string& label,
                             const std::deque<double>& data,
                             int w = PLOT_W, int h = 4) {
    if (data.empty()) { std::cout << "  [" << label << ": no data]\n"; return; }
    double mn = *std::min_element(data.begin(), data.end());
    double mx = *std::max_element(data.begin(), data.end());
    if (mx - mn < 1e-9) mx = mn + 1.0;

    std::cout << "  " << std::setw(16) << std::left << label
              << " min=" << std::fixed << std::setprecision(2) << mn
              << " max=" << mx << "\n";

    int cols = std::min(static_cast<int>(data.size()), w);
    int offset = static_cast<int>(data.size()) - cols;
    for (int row = h - 1; row >= 0; --row) {
        std::cout << "  |";
        for (int col = 0; col < cols; ++col) {
            double norm = (data[offset + col] - mn) / (mx - mn);
            int bar = static_cast<int>(norm * (h - 1) + 0.5);
            std::cout << (bar >= row ? '*' : ' ');
        }
        for (int col = cols; col < w; ++col) std::cout << ' ';
        std::cout << "|\n";
    }
    std::cout << "  +" << std::string(w, '-') << "+\n";
}

// ---------------------------------------------------------------------------
// Render helpers
// ---------------------------------------------------------------------------
static void printModeBar(DisplayMode mode, const std::string& extra = "") {
    std::cout << "\033[2J\033[H";
    std::cout << "=== COUNTER-UAS RADAR TRACKER  |  " << nowStr()
              << "  |  [" << modeName(mode) << "]"
              << (extra.empty() ? "" : "  " + extra) << " ===\n";
    std::cout << " 1=RawDets  2=Clusters  3=Assoc  4=Predicted  5=Tracks"
                 "  6=TrkFilter  7=PPI  8=BScope  9=CScope  0=TimeSeries  Q=Quit\n";
    std::cout << std::string(82, '-') << "\n";
}

// 1: Raw Detections
static void renderRawDetections(const cuas::SPDetectionMessage& dets, uint64_t msgs) {
    printModeBar(DisplayMode::RawDetections,
                 "Dwell#" + std::to_string(dets.dwellCount) + "  Msgs:" + std::to_string(msgs));
    if (dets.numDetections == 0) { std::cout << "  (waiting for first dwell...)\n"; return; }
    std::cout << std::setw(5)  << "#"
              << std::setw(11) << "Range(m)"
              << std::setw(10) << "Az(deg)"
              << std::setw(10) << "El(deg)"
              << std::setw(10) << "SNR(dB)"
              << std::setw(10) << "RCS(dBsm)"
              << std::setw(11) << "uDop(Hz)"
              << std::setw(9)  << "X(m)"
              << std::setw(9)  << "Y(m)"
              << std::setw(9)  << "Z(m)"
              << "\n" << std::string(94, '-') << "\n";
    int shown = 0;
    for (const auto& d : dets.detections) {
        auto c = cuas::sphericalToCartesian(d.range, d.azimuth, d.elevation);
        std::cout << std::fixed
                  << std::setw(5)  << ++shown
                  << std::setw(11) << std::setprecision(1) << d.range
                  << std::setw(10) << std::setprecision(2) << d.azimuth   * cuas::RAD2DEG
                  << std::setw(10) << std::setprecision(2) << d.elevation * cuas::RAD2DEG
                  << std::setw(10) << std::setprecision(1) << d.snr
                  << std::setw(10) << std::setprecision(1) << d.rcs
                  << std::setw(11) << std::setprecision(1) << d.microDoppler
                  << std::setw(9)  << std::setprecision(1) << c.x
                  << std::setw(9)  << std::setprecision(1) << c.y
                  << std::setw(9)  << std::setprecision(1) << c.z << "\n";
        if (shown >= 25) {
            if (dets.numDetections > 25) std::cout << "  ... (" << dets.numDetections - 25 << " more)\n";
            break;
        }
    }
}

// 2: Clusters
static void renderClusters(const std::vector<cuas::ClusterWire>& clusters,
                            uint32_t dwellCount, uint64_t msgs) {
    printModeBar(DisplayMode::Clusters,
                 "Dwell#" + std::to_string(dwellCount) + "  Msgs:" + std::to_string(msgs));
    if (clusters.empty()) { std::cout << "  (no clusters)\n"; return; }
    std::cout << std::setw(6)  << "ID"
              << std::setw(6)  << "NDet"
              << std::setw(11) << "Range(m)"
              << std::setw(10) << "Az(deg)"
              << std::setw(10) << "El(deg)"
              << std::setw(10) << "SNR(dB)"
              << std::setw(10) << "RCS(dBsm)"
              << std::setw(11) << "uDop(Hz)"
              << std::setw(9)  << "X(m)"
              << std::setw(9)  << "Y(m)"
              << std::setw(9)  << "Z(m)"
              << "\n" << std::string(101, '-') << "\n";
    for (const auto& c : clusters) {
        std::cout << std::fixed
                  << std::setw(6)  << c.clusterId
                  << std::setw(6)  << c.numDetections
                  << std::setw(11) << std::setprecision(1) << c.range
                  << std::setw(10) << std::setprecision(2) << c.azimuth   * cuas::RAD2DEG
                  << std::setw(10) << std::setprecision(2) << c.elevation * cuas::RAD2DEG
                  << std::setw(10) << std::setprecision(1) << c.snr
                  << std::setw(10) << std::setprecision(1) << c.rcs
                  << std::setw(11) << std::setprecision(1) << c.microDoppler
                  << std::setw(9)  << std::setprecision(1) << c.x
                  << std::setw(9)  << std::setprecision(1) << c.y
                  << std::setw(9)  << std::setprecision(1) << c.z << "\n";
    }
    std::cout << std::string(101, '-') << "\n";
    std::cout << "Total clusters: " << clusters.size() << "\n";
}

// 3: Association
static void renderAssociation(const std::vector<cuas::AssocEntryWire>& assoc, uint64_t msgs) {
    printModeBar(DisplayMode::Association, "Msgs:" + std::to_string(msgs));
    if (assoc.empty()) { std::cout << "  (no association data)\n"; return; }
    std::cout << std::setw(10) << "TrackID"
              << std::setw(10) << "ClusterID"
              << std::setw(14) << "Distance"
              << "  Status\n"
              << std::string(50, '-') << "\n";
    int matched = 0, unmTrack = 0, unmClust = 0;
    for (const auto& e : assoc) {
        std::string st;
        if (e.matched) { ++matched; st = "MATCHED"; }
        else if (e.clusterId == 0xFFFFFFFFu) { ++unmTrack;  st = "UNMATCHED-TRACK"; }
        else                                  { ++unmClust;  st = "UNMATCHED-CLUST"; }
        std::cout << std::fixed
                  << std::setw(10) << (e.trackId   == 0xFFFFFFFFu ? 0u : e.trackId)
                  << std::setw(10) << (e.clusterId == 0xFFFFFFFFu ? 0u : e.clusterId)
                  << std::setw(14) << std::setprecision(3) << (e.distance < 0 ? -1.0 : e.distance)
                  << "  " << st << "\n";
    }
    std::cout << std::string(50, '-') << "\n";
    std::cout << "Matched: " << matched
              << "  Unmatched tracks: " << unmTrack
              << "  Unmatched clusters: " << unmClust << "\n";
}

// 4: Predicted States
static void renderPredicted(const std::vector<cuas::PredictedEntryWire>& preds, uint64_t msgs) {
    printModeBar(DisplayMode::Predicted, "Msgs:" + std::to_string(msgs));
    if (preds.empty()) { std::cout << "  (no predicted data)\n"; return; }
    static const char* snames[] = {"TENT","CONF","COAS","DEL","?"};
    std::cout << std::setw(6)  << "TrkID"
              << std::setw(5)  << "Stat"
              << std::setw(9)  << "Rng(m)"
              << std::setw(8)  << "Az(d)"
              << std::setw(8)  << "El(d)"
              << std::setw(9)  << "X(m)"
              << std::setw(9)  << "Y(m)"
              << std::setw(9)  << "Z(m)"
              << std::setw(7)  << "Vx"
              << std::setw(7)  << "Vy"
              << std::setw(7)  << "Vz"
              << "  CV  CA1  CA2 CTR1 CTR2   sX   sY   sZ"
              << "\n" << std::string(115, '-') << "\n";
    for (const auto& pe : preds) {
        const char* sn = (pe.trackStatus < 4) ? snames[pe.trackStatus] : "?";
        std::cout << std::fixed
                  << std::setw(6)  << pe.trackId
                  << std::setw(5)  << sn
                  << std::setw(9)  << std::setprecision(1) << pe.range
                  << std::setw(8)  << std::setprecision(1) << pe.azimuth   * cuas::RAD2DEG
                  << std::setw(8)  << std::setprecision(1) << pe.elevation * cuas::RAD2DEG
                  << std::setw(9)  << std::setprecision(1) << pe.x
                  << std::setw(9)  << std::setprecision(1) << pe.y
                  << std::setw(9)  << std::setprecision(1) << pe.z
                  << std::setw(7)  << std::setprecision(1) << pe.vx
                  << std::setw(7)  << std::setprecision(1) << pe.vy
                  << std::setw(7)  << std::setprecision(1) << pe.vz;
        for (int m = 0; m < 5; ++m)
            std::cout << std::setw(5) << std::setprecision(2) << pe.modelProb[m];
        std::cout << std::setw(5) << std::setprecision(1) << std::sqrt(std::max(0.0, pe.covX))
                  << std::setw(5) << std::setprecision(1) << std::sqrt(std::max(0.0, pe.covY))
                  << std::setw(5) << std::setprecision(1) << std::sqrt(std::max(0.0, pe.covZ))
                  << "\n";
    }
}

// 5: All Tracks
static void renderAllTracks(const std::vector<cuas::TrackUpdateMessage>& tracks, uint64_t msgs) {
    printModeBar(DisplayMode::AllTracks,
                 std::to_string(tracks.size()) + " tracks  Msgs:" + std::to_string(msgs));
    if (tracks.empty()) { std::cout << "  (no tracks yet)\n"; return; }
    std::cout << std::setw(5)  << "ID"
              << std::setw(6)  << "Stat"
              << std::setw(8)  << "Class"
              << std::setw(10) << "Range(m)"
              << std::setw(9)  << "Az(deg)"
              << std::setw(9)  << "El(deg)"
              << std::setw(9)  << "Rdot"
              << std::setw(8)  << "X(m)"
              << std::setw(8)  << "Y(m)"
              << std::setw(8)  << "Z(m)"
              << std::setw(7)  << "Qual"
              << std::setw(4)  << "Hit"
              << std::setw(4)  << "Mis"
              << std::setw(4)  << "Age"
              << "\n" << std::string(104, '-') << "\n";
    int conf = 0, tent = 0, coast = 0;
    for (const auto& t : tracks) {
        if      (t.status == cuas::TrackStatus::Confirmed) ++conf;
        else if (t.status == cuas::TrackStatus::Tentative) ++tent;
        else if (t.status == cuas::TrackStatus::Coasting)  ++coast;
        std::cout << std::fixed
                  << std::setw(5)  << t.trackId
                  << std::setw(6)  << statusStr(t.status)
                  << std::setw(8)  << classStr(t.classification)
                  << std::setw(10) << std::setprecision(1) << t.range
                  << std::setw(9)  << std::setprecision(2) << t.azimuth   * cuas::RAD2DEG
                  << std::setw(9)  << std::setprecision(2) << t.elevation * cuas::RAD2DEG
                  << std::setw(9)  << std::setprecision(1) << t.rangeRate
                  << std::setw(8)  << std::setprecision(1) << t.x
                  << std::setw(8)  << std::setprecision(1) << t.y
                  << std::setw(8)  << std::setprecision(1) << t.z
                  << std::setw(7)  << std::setprecision(2) << t.trackQuality
                  << std::setw(4)  << t.hitCount
                  << std::setw(4)  << t.missCount
                  << std::setw(4)  << t.age << "\n";
    }
    std::cout << std::string(104, '-') << "\n";
    std::cout << "Summary: " << conf << " confirmed  " << tent << " tentative  " << coast << " coasting\n";
}

// 6: Track Filter
static void renderTrackFilter(const std::vector<cuas::TrackUpdateMessage>& tracks,
                               const std::map<uint32_t, TrackHistory>& hist,
                               uint32_t filterId, bool inputMode, const std::string& inputBuf) {
    printModeBar(DisplayMode::TrackFilter,
                 inputMode ? ("Enter ID: " + inputBuf + "_")
                           : ("Track #" + std::to_string(filterId)));
    if (inputMode) {
        std::cout << "  Type a numeric track ID and press Enter.\n"
                  << "  Current filter: " << (filterId == 0 ? "(none)" : std::to_string(filterId)) << "\n";
        return;
    }
    const cuas::TrackUpdateMessage* found = nullptr;
    for (const auto& t : tracks) if (t.trackId == filterId) { found = &t; break; }
    if (!found) {
        std::cout << "  Track #" << filterId << " not found. Press '6' to enter an ID.\n";
    } else {
        const auto& t = *found;
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  Track #" << t.trackId
                  << "  Status: " << statusStr(t.status)
                  << "  Class: " << classStr(t.classification) << "\n\n";
        std::cout << "  Range:     " << t.range << " m\n";
        std::cout << "  Azimuth:   " << t.azimuth   * cuas::RAD2DEG << " deg\n";
        std::cout << "  Elevation: " << t.elevation * cuas::RAD2DEG << " deg\n";
        std::cout << "  Rdot:      " << t.rangeRate << " m/s\n";
        std::cout << "  Position:  X=" << t.x << "  Y=" << t.y << "  Z=" << t.z << " m\n";
        std::cout << "  Velocity:  Vx=" << t.vx << "  Vy=" << t.vy << "  Vz=" << t.vz << " m/s\n";
        std::cout << "  Quality:   " << t.trackQuality
                  << "  Hits: " << t.hitCount << "  Misses: " << t.missCount << "  Age: " << t.age << "\n\n";
    }
    auto it = hist.find(filterId);
    if (it != hist.end()) {
        const auto& h = it->second;
        std::cout << "  --- History (" << h.range.size() << " samples) ---\n";
        printTimeSeries("Range (m)",   h.range,        PLOT_W, 4);
        printTimeSeries("Az (deg)",    h.azimuthDeg,   PLOT_W, 4);
        printTimeSeries("El (deg)",    h.elevationDeg, PLOT_W, 3);
        printTimeSeries("Rdot (m/s)", h.rangeRate,    PLOT_W, 3);
        printTimeSeries("Quality",    h.quality,      PLOT_W, 3);
    } else {
        std::cout << "  (no history for track #" << filterId << ")\n";
    }
}

// 7: PPI
static void renderPPI(const cuas::SPDetectionMessage& dets,
                      const std::vector<cuas::TrackUpdateMessage>& tracks) {
    printModeBar(DisplayMode::PPI, "PPI top-down  o=det  T=confirmed  t=tentative");
    double maxRange = 1000.0;
    for (const auto& d : dets.detections) maxRange = std::max(maxRange, d.range);
    for (const auto& t : tracks)           maxRange = std::max(maxRange, t.range);
    maxRange = std::ceil(maxRange / 500.0) * 500.0;

    Grid grid = makeGrid(PLOT_W, PLOT_H);
    double cx = PLOT_W / 2.0, cy = PLOT_H / 2.0;

    // Range rings
    for (int ring = 1; ring <= 4; ++ring) {
        double rn = ring / 4.0;
        for (int th = 0; th < 360; ++th) {
            double rad = th * cuas::DEG2RAD;
            int px = static_cast<int>(cx + rn * (cx - 1) * std::cos(rad) + 0.5);
            int py = static_cast<int>(cy - rn * (cy - 1) * std::sin(rad) * 0.55 + 0.5);
            plotPoint(grid, px, py, ring == 4 ? 'o' : '.');
        }
        // Label
        int lx = static_cast<int>(cx + rn * (cx - 1) + 1);
        int ly = static_cast<int>(cy);
        for (char ch : std::to_string(static_cast<int>(maxRange * rn / 1000.0)) + "k")
            plotPoint(grid, lx++, ly, ch);
    }
    for (int x = 0; x < PLOT_W; ++x) plotPoint(grid, x, static_cast<int>(cy), '-');
    for (int y = 0; y < PLOT_H;  ++y) plotPoint(grid, static_cast<int>(cx), y, '|');
    plotPoint(grid, static_cast<int>(cx), static_cast<int>(cy), 'R');

    for (const auto& d : dets.detections) {
        double nx = d.range * std::cos(d.azimuth) / maxRange;
        double ny = d.range * std::sin(d.azimuth) / maxRange;
        int px = static_cast<int>(cx + nx * (cx - 1) + 0.5);
        int py = static_cast<int>(cy - ny * (cy - 1) * 0.55 + 0.5);
        plotPoint(grid, px, py, '.');
    }
    for (const auto& t : tracks) {
        double nx = t.x / maxRange, ny = t.y / maxRange;
        int px = static_cast<int>(cx + nx * (cx - 1) + 0.5);
        int py = static_cast<int>(cy - ny * (cy - 1) * 0.55 + 0.5);
        char ch = (t.status == cuas::TrackStatus::Confirmed) ? 'T' : 't';
        plotPoint(grid, px, py, ch);
        int lx = px + 1;
        for (char c : std::to_string(t.trackId)) plotPoint(grid, lx++, py, c);
    }

    std::cout << "  Max range: " << maxRange << " m\n";
    printGrid(grid);
    std::cout << "  .=raw det  T=confirmed  t=tentative  R=radar  .-=range rings\n";
}

// 8: B-Scope
static void renderBScope(const cuas::SPDetectionMessage& dets,
                          const std::vector<cuas::TrackUpdateMessage>& tracks) {
    printModeBar(DisplayMode::BScope, "B-Scope  X=Azimuth  Y=Range  o=det T=track");
    double maxRange = 1000.0;
    for (const auto& d : dets.detections) maxRange = std::max(maxRange, d.range);
    for (const auto& t : tracks)           maxRange = std::max(maxRange, t.range);
    maxRange = std::ceil(maxRange / 500.0) * 500.0;

    Grid grid = makeGrid(PLOT_W, PLOT_H);
    // Grid lines
    for (int az = -150; az <= 180; az += 30) {
        double norm = (az + 180.0) / 360.0;
        int px = static_cast<int>(norm * (PLOT_W - 1) + 0.5);
        for (int y = 0; y < PLOT_H; ++y) plotPoint(grid, px, y, ':');
        std::string lab = std::to_string(az);
        int ly = PLOT_H - 1;
        for (char ch : lab) plotPoint(grid, px + 1, ly, ch);
    }
    for (int ring = 1; ring <= 4; ++ring) {
        double norm = ring / 4.0;
        int py = PLOT_H - 1 - static_cast<int>(norm * (PLOT_H - 1));
        for (int x = 0; x < PLOT_W; ++x) plotPoint(grid, x, py, '-');
        std::string lab = std::to_string(static_cast<int>(maxRange * norm / 1000.0)) + "k";
        int lx = 0;
        for (char ch : lab) plotPoint(grid, lx++, py, ch);
    }
    for (const auto& d : dets.detections) {
        double normAz = (d.azimuth * cuas::RAD2DEG + 180.0) / 360.0;
        double normR  = d.range / maxRange;
        int px = static_cast<int>(normAz * (PLOT_W - 1) + 0.5);
        int py = PLOT_H - 1 - static_cast<int>(normR  * (PLOT_H - 1) + 0.5);
        plotPoint(grid, px, py, '.');
    }
    for (const auto& t : tracks) {
        double normAz = (t.azimuth * cuas::RAD2DEG + 180.0) / 360.0;
        double normR  = t.range / maxRange;
        int px = static_cast<int>(normAz * (PLOT_W - 1) + 0.5);
        int py = PLOT_H - 1 - static_cast<int>(normR  * (PLOT_H - 1) + 0.5);
        char ch = (t.status == cuas::TrackStatus::Confirmed) ? 'T' : 't';
        plotPoint(grid, px, py, ch);
    }
    std::cout << "  Az: -180 to +180 deg  |  Range: 0 to " << maxRange << " m\n";
    printGrid(grid);
}

// 9: C-Scope
static void renderCScope(const cuas::SPDetectionMessage& dets,
                          const std::vector<cuas::TrackUpdateMessage>& tracks) {
    printModeBar(DisplayMode::CScope, "C-Scope  X=Azimuth  Y=Elevation  o=det T=track");
    const double AZ_MIN = -180.0, AZ_MAX = 180.0;
    const double EL_MIN =  -10.0, EL_MAX =  90.0;

    Grid grid = makeGrid(PLOT_W, PLOT_H);
    for (int az = -150; az <= 180; az += 30) {
        double norm = (az - AZ_MIN) / (AZ_MAX - AZ_MIN);
        int px = static_cast<int>(norm * (PLOT_W - 1) + 0.5);
        for (int y = 0; y < PLOT_H; ++y) plotPoint(grid, px, y, ':');
    }
    for (int el = 0; el <= 90; el += 10) {
        double norm = (el - EL_MIN) / (EL_MAX - EL_MIN);
        int py = PLOT_H - 1 - static_cast<int>(norm * (PLOT_H - 1) + 0.5);
        for (int x = 0; x < PLOT_W; ++x) plotPoint(grid, x, py, '-');
        std::string lab = std::to_string(el) + "d";
        int lx = 0;
        for (char ch : lab) plotPoint(grid, lx++, py, ch);
    }
    for (const auto& d : dets.detections) {
        double azD = d.azimuth * cuas::RAD2DEG, elD = d.elevation * cuas::RAD2DEG;
        double normAz = (azD - AZ_MIN) / (AZ_MAX - AZ_MIN);
        double normEl = (elD - EL_MIN) / (EL_MAX - EL_MIN);
        int px = static_cast<int>(normAz * (PLOT_W - 1) + 0.5);
        int py = PLOT_H - 1 - static_cast<int>(normEl * (PLOT_H - 1) + 0.5);
        plotPoint(grid, px, py, '.');
    }
    for (const auto& t : tracks) {
        double azD = t.azimuth * cuas::RAD2DEG, elD = t.elevation * cuas::RAD2DEG;
        double normAz = (azD - AZ_MIN) / (AZ_MAX - AZ_MIN);
        double normEl = (elD - EL_MIN) / (EL_MAX - EL_MIN);
        int px = static_cast<int>(normAz * (PLOT_W - 1) + 0.5);
        int py = PLOT_H - 1 - static_cast<int>(normEl * (PLOT_H - 1) + 0.5);
        char ch = (t.status == cuas::TrackStatus::Confirmed) ? 'T' : 't';
        plotPoint(grid, px, py, ch);
    }
    std::cout << "  Az: " << AZ_MIN << " to " << AZ_MAX << " deg  |  El: " << EL_MIN << " to " << EL_MAX << " deg\n";
    printGrid(grid);
}

// 0: Time Series
static void renderTimeSeries(const std::map<uint32_t, TrackHistory>& hist,
                              const std::vector<cuas::TrackUpdateMessage>& tracks) {
    printModeBar(DisplayMode::TimeSeries, "Time Series  last " + std::to_string(HISTORY_LEN) + " samples");
    if (hist.empty()) { std::cout << "  (no track history yet)\n"; return; }
    std::vector<uint32_t> ids;
    for (const auto& t : tracks)
        if (t.status == cuas::TrackStatus::Confirmed) ids.push_back(t.trackId);
    if (ids.empty()) for (const auto& kv : hist) ids.push_back(kv.first);
    if (ids.size() > 3) ids.resize(3);
    for (uint32_t tid : ids) {
        auto it = hist.find(tid);
        if (it == hist.end()) continue;
        const auto& h = it->second;
        std::cout << "\n  === Track #" << tid << " (" << h.range.size() << " samples) ===\n";
        printTimeSeries("Range (m)",  h.range,        PLOT_W, 4);
        printTimeSeries("Az (deg)",   h.azimuthDeg,   PLOT_W, 3);
        printTimeSeries("El (deg)",   h.elevationDeg, PLOT_W, 3);
        printTimeSeries("Rdot (m/s)", h.rangeRate,    PLOT_W, 3);
    }
    if (ids.size() < hist.size())
        std::cout << "  (showing " << ids.size() << "/" << hist.size()
                  << " tracks — use Track Filter [6] for details)\n";
}

// ---------------------------------------------------------------------------
// Master render dispatcher
// ---------------------------------------------------------------------------
static void render() {
    switch (g_mode) {
        case DisplayMode::RawDetections: renderRawDetections(g_latestDets, g_detMsgCount);    break;
        case DisplayMode::Clusters:      renderClusters(g_latestClusters, g_lastDwellCount, g_clusterMsgCount); break;
        case DisplayMode::Association:   renderAssociation(g_latestAssoc, g_assocMsgCount);   break;
        case DisplayMode::Predicted:     renderPredicted(g_latestPredicted, g_predMsgCount);  break;
        case DisplayMode::AllTracks:     renderAllTracks(g_latestTracks, g_trackMsgCount);    break;
        case DisplayMode::TrackFilter:
            renderTrackFilter(g_latestTracks, g_trackHistory,
                              g_filterTrackId, g_filterInputMode, g_filterInput);
            break;
        case DisplayMode::PPI:           renderPPI(g_latestDets, g_latestTracks);    break;
        case DisplayMode::BScope:        renderBScope(g_latestDets, g_latestTracks);  break;
        case DisplayMode::CScope:        renderCScope(g_latestDets, g_latestTracks);  break;
        case DisplayMode::TimeSeries:    renderTimeSeries(g_trackHistory, g_latestTracks); break;
    }
    std::cout << std::flush;
}

// ---------------------------------------------------------------------------
// Process keypress
// ---------------------------------------------------------------------------
static void handleKey(int ch) {
    if (g_filterInputMode) {
        if (ch == '\r' || ch == '\n') {
            if (!g_filterInput.empty())
                g_filterTrackId = static_cast<uint32_t>(std::stoul(g_filterInput));
            g_filterInput.clear();
            g_filterInputMode = false;
        } else if (ch == 8 || ch == 127) {
            if (!g_filterInput.empty()) g_filterInput.pop_back();
        } else if (ch >= '0' && ch <= '9') {
            g_filterInput += static_cast<char>(ch);
        }
        return;
    }
    switch (ch) {
        case '1': g_mode = DisplayMode::RawDetections; break;
        case '2': g_mode = DisplayMode::Clusters;      break;
        case '3': g_mode = DisplayMode::Association;   break;
        case '4': g_mode = DisplayMode::Predicted;     break;
        case '5': g_mode = DisplayMode::AllTracks;     break;
        case '6': g_mode = DisplayMode::TrackFilter; g_filterInputMode = true; g_filterInput.clear(); break;
        case '7': g_mode = DisplayMode::PPI;           break;
        case '8': g_mode = DisplayMode::BScope;        break;
        case '9': g_mode = DisplayMode::CScope;        break;
        case '0': g_mode = DisplayMode::TimeSeries;    break;
        case 'q': case 'Q': g_running.store(false);    break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    int listenPort = 50001;
    if (argc > 1) listenPort = std::stoi(argv[1]);

    std::signal(SIGINT, signalHandler);
#ifndef _WIN32
    std::signal(SIGTERM, signalHandler);
    setRawMode();
#endif

    cuas::ConsoleLogger::instance().setLevel(cuas::ConsoleLogger::WARN);

    std::cerr <<
        "================================================================\n"
        "  Counter-UAS Radar Tracker  —  Multi-Mode Display\n"
        "  Listening on UDP port " << listenPort << "\n"
        "  Keys: 1-9,0=mode  6+ID+Enter=filter  Q=quit\n"
        "================================================================\n\n";

    cuas::UdpSocket::initNetwork();
    cuas::UdpSocket socket;

    if (!socket.bindSocket("0.0.0.0", listenPort)) {
        std::cerr << "ERROR: Failed to bind on port " << listenPort << "\n";
        cuas::UdpSocket::cleanupNetwork();
#ifndef _WIN32
        restoreMode();
#endif
        return 1;
    }
    socket.setReceiveTimeout(300);

    std::vector<uint8_t> buf(65536);

    while (g_running.load()) {
        int ch = kbCheck();
        if (ch >= 0) handleKey(ch);

        int n = socket.receive(buf.data(), static_cast<int>(buf.size()));

        if (n >= 4) {
            uint32_t msgId = 0;
            std::memcpy(&msgId, buf.data(), 4);

            if (msgId == cuas::MSG_ID_TRACK_TABLE) {
                std::vector<cuas::TrackUpdateMessage> tracks;
                uint64_t ts = 0;
                if (cuas::MessageSerializer::deserializeTrackTable(buf.data(), n, tracks, ts)) {
                    g_latestTracks = std::move(tracks);
                    ++g_trackMsgCount;
                    for (const auto& t : g_latestTracks) {
                        auto& h = g_trackHistory[t.trackId];
                        h.range.push_back(t.range);
                        h.azimuthDeg.push_back(t.azimuth * cuas::RAD2DEG);
                        h.elevationDeg.push_back(t.elevation * cuas::RAD2DEG);
                        h.rangeRate.push_back(t.rangeRate);
                        h.quality.push_back(t.trackQuality);
                        if ((int)h.range.size()        > HISTORY_LEN) h.range.pop_front();
                        if ((int)h.azimuthDeg.size()   > HISTORY_LEN) h.azimuthDeg.pop_front();
                        if ((int)h.elevationDeg.size() > HISTORY_LEN) h.elevationDeg.pop_front();
                        if ((int)h.rangeRate.size()    > HISTORY_LEN) h.rangeRate.pop_front();
                        if ((int)h.quality.size()      > HISTORY_LEN) h.quality.pop_front();
                    }
                }

            } else if (msgId == cuas::MSG_ID_SP_DETECTION) {
                cuas::SPDetectionMessage detMsg;
                if (cuas::MessageSerializer::deserialize(buf.data(), n, detMsg)) {
                    g_latestDets = std::move(detMsg);
                    ++g_detMsgCount;
                }

            } else if (msgId == cuas::MSG_ID_CLUSTER_TABLE) {
                std::vector<cuas::ClusterWire> clusters;
                uint64_t ts = 0; uint32_t dwellCount = 0;
                if (cuas::MessageSerializer::deserializeClusterTable(buf.data(), n, clusters, ts, dwellCount)) {
                    g_latestClusters = std::move(clusters);
                    g_lastDwellCount = dwellCount;
                    ++g_clusterMsgCount;
                }

            } else if (msgId == cuas::MSG_ID_ASSOC_TABLE) {
                std::vector<cuas::AssocEntryWire> assoc;
                uint64_t ts = 0;
                if (cuas::MessageSerializer::deserializeAssocTable(buf.data(), n, assoc, ts)) {
                    g_latestAssoc = std::move(assoc);
                    ++g_assocMsgCount;
                }

            } else if (msgId == cuas::MSG_ID_PREDICTED_TABLE) {
                std::vector<cuas::PredictedEntryWire> preds;
                uint64_t ts = 0;
                if (cuas::MessageSerializer::deserializePredictedTable(buf.data(), n, preds, ts)) {
                    g_latestPredicted = std::move(preds);
                    ++g_predMsgCount;
                }

            } else if (msgId == cuas::MSG_ID_TRACK_UPDATE) {
                cuas::TrackUpdateMessage single;
                if (cuas::MessageSerializer::deserialize(buf.data(), n, single)) {
                    g_latestTracks = { single };
                    ++g_trackMsgCount;
                }
            }
        }

        render();
    }

#ifndef _WIN32
    restoreMode();
#endif
    std::cerr << "\nExiting.  Det=" << g_detMsgCount
              << "  Tracks=" << g_trackMsgCount
              << "  Clusters=" << g_clusterMsgCount
              << "  Assoc=" << g_assocMsgCount
              << "  Pred=" << g_predMsgCount << "\n";
    cuas::UdpSocket::cleanupNetwork();
    return 0;
}
