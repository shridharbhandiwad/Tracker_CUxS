# Technical Design Documentation (PowerPoint)

This repository includes a generated Technical Design Deck (TDD) in PowerPoint format:

- `docs/CounterUAS_RadarTracker_TDD.pptx`

It covers:

- System context / deployment view (DSP → tracker → display)
- Component/module architecture (from `CMakeLists.txt` targets)
- UML (high-level class + strategy interfaces)
- Runtime sequence + per-dwell processing data flow
- Track lifecycle state machine
- Key software principles/patterns used in the design

## Regenerating the deck

Prerequisites:

- Graphviz `dot`
- Python 3
- Python deps: `python-pptx`, `pillow`

Install deps:

```bash
python3 -m pip install -r docs/scripts/requirements.txt
```

Generate diagrams + PPTX:

```bash
python3 docs/scripts/generate_tdd_ppt.py
```

## Diagram sources

Diagram source (`.dot`) files live under `docs/diagrams/`. PNGs are rendered from these sources and embedded into the PPTX.

