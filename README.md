# NN3 Visuals

Work in progress. This repository contains an openFrameworks C++ project for real-time visuals and show-control. Expect breaking changes and incomplete features while development continues.

## Current Features (WIP)
- 1080x3840 internal render FBO with a 540x1920 preview window.
- NDI input selection (dropdown with `<none>`), NDI output (`NN3_COMPOSITE`).
- Layered compositor: NDI layer, particle systems, foam layers.
- Foam shader layers (FBO-based) with per-layer fade.
- Particle systems with adjustable size/speed/bounce/amount/life/trail/fade/noise/noise-start.
- NDI mask interaction: particles bounce off incoming NDI white masks.
- GUI: NDI input, foam, and particles controls; layer selection, move, delete; global visibility toggles.
- Composition save/load to `bin/data/presets/composition.json`.

## Controls
- Drag layers in the preview to reposition.
- Mouse wheel on selected FOAM or PARTICLES to resize.
- `V` toggles all layer borders.
- `G` toggles the GUI.
- `S` saves composition, `L` loads composition.

## Project Layout (high level)
- `src/` main openFrameworks app (`ofApp`).
- `bin/data/` shaders, presets, and assets.

## Notes
- This is an active build for an installation pipeline (NDI, OSC, DMX planned).
- Stability/perf work is ongoing; expect changes in shaders and particle behavior.
- Do not treat this as a stable API or final feature set yet.
