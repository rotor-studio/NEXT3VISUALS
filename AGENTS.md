# Repository Guidelines

You are a senior creative coder / realtime systems engineer specialized in openFrameworks installations. Build stable 24/7 interactive artworks with heavy NDI video pipelines, GLSL multi-pass rendering, and OSC + DMX show-control with robust failover.

## High-Level Architecture
- Three buses with explicit ownership: Video (render timing), Control (state/params), Lighting (fixed DMX rate).
- Video bus: NDI In -> decode/convert -> RenderGraph (FBO chain) -> Compositor -> Mapper/Warp -> outputs (screen + NDI Out).
- Control bus: OSC In/Out -> ParameterStore -> SceneManager -> PresetManager; drives render + DMX parameters.
- Lighting bus: DMX In/Out -> fixture patch -> output frame at target DMX rate; never blocked by render FPS.
- Update order: I/O threads collect -> main thread integrates -> render graph -> outputs -> diagnostics.

## Suggested Folder Structure
- `src/`
  - `app/` (ofApp, app bootstrap)
  - `core/` (ParameterStore, SceneManager, PresetManager, Diagnostics)
  - `pipeline/` (RenderGraph, Pass, Compositor)
  - `io/ndi/`, `io/osc/`, `io/dmx/`
  - `mapping/` (WarpManager, masks)
  - `rendering/` (FBO utils, shader helpers)
  - `scenes/`, `ui_debug/`, `utils/`
- `bin/data/`
  - `shaders/`, `masks/`, `warp/`, `luts/`, `configs/`, `presets/`, `fixtures/`, `logs/`

## Core Class Blueprint
- `NDIManager`: inputs/outputs, async receive, reconnect, frame buffers.
- `RenderGraph` + `Pass`: multi-pass FBO pipeline, reuse textures.
- `Compositor`: layer blend, masks, output routing.
- `WarpManager`: per-output warps, calibration, persistence.
- `OSCManager`: in/out, address routing, param mapping, telemetry.
- `DMXManager`: patch model, universe I/O, frame clock, safety.
- `ParameterStore`: typed params, ranges, smoothing, serialization.
- `SceneManager`: cues, states, transitions, automation.
- `PresetManager`: JSON save/load, snapshot versioning.
- `Diagnostics`: FPS, dropped frames, health checks, logs, overlay.

## OSC Design Guidance
- Addressing: `/app/scene/*`, `/video/*`, `/fx/*`, `/dmx/*`, `/system/*`.
- Map OSC to typed parameters with min/max and smoothing; reject invalid ranges.
- Use cues for discrete transitions, continuous control for live tweaks; default to cues for stability.
- Telemetry out: FPS, NDI status, dropped frames, DMX rate, active scene.

## DMX Design Guidance
- Patch model: fixtures -> channels -> universes; keep JSON in `bin/data/fixtures/`.
- Update rate: fixed timer (e.g., 30-44 Hz); never tied to render FPS.
- Safety: blackout mode, hold-last, and fallback look; choose per-fixture policy.
- DMX params are mirrored into `ParameterStore` and recorded in presets.

## NDI + Videomapping Guidance
- Pixel format conversion happens on ingest before the RenderGraph.
- If NDI stalls: async capture with timeouts, last-frame hold, reconnection backoff.
- Provide preview + program outputs; program goes through full warp/mask.
- Place warp/mask after compositing, before NDI Out and screen draw.

## Performance & Stability Checklist
- No per-frame allocations; reuse FBOs, textures, and OSC buffers.
- Threading: NDI receive thread, OSC thread, DMX thread; main thread renders.
- Frame timing: render at target FPS; decouple DMX and OSC.
- Exhibition mode: auto-reconnect, shader hot-reload, safe defaults on failure.
- Logging: ring buffer logs + periodic health snapshots.

## Working Notes (Session)
- Particle trail flicker: avoid sampling a stale `prevTrailPos`. Keep trail positions coherent when rebounding or teleporting by resetting both `trailPos` and `prevTrailPos` at the collision point. If flicker returns with high trail values, prefer using a stable trail anchor (current trail position) and clamp large deltas.
- Mist mode: FOAM layers can be created as vertical mist via the FOAM “M” toggle, with a separate mist shader and a global mist speed slider; mist layers should not participate in particle bounce.
- NDI test: the test pattern uses a flat gray background and centered TTF text (no heavy grid), and the white framing border must be drawn only in the preview (not in the NDI output).
- Performance: throttle NDI mask readback and avoid per-pixel `ofColor` calls in hot loops; skip FBO updates for disabled foam layers.
- Presets: 5 preset buttons (click=load, shift+click=save, alt+click=clear), with a global transition slider (fade out → black → load → black → fade in). Presets do not alter global NDI selection or transition time.
- NDI selection: store desired sender name and attempt reconnect when sender appears; apply on general load even if sender list is empty.
- Particles: global spawn toggle (green/red) should stop new spawns immediately and never be saved/loaded; existing particles finish naturally.
- UI spacing: keep generous vertical spacing between NDI/FOAM/PARTICLES and between preset controls to avoid overlap.
- Foam bounce width: FOAM slider now controls horizontal bounce width (0-100%, centered), not Y position. Default is full width and it is saved per FOAM in presets.
- NDI status: dropdown background is green when the selected sender is connected, red when selected but unavailable, gray when <none>.
- NDI resilience: keep desired sender name even if offline; release receiver when unavailable and reconnect when the sender reappears. Do not crash if the source stops.
- Performance: particles skip trail buffer updates when trail=0 and precompute common draw values per system to reduce per-particle overhead.
- Performance tests: FOAM updates every 2 frames for performance. Preview draw toggle lives under COLORS as "BETTER FPS". UI lock toggle lives under that ("LOCK UI") and blocks edits; key `K` also toggles it. Key `P` toggles a compact mode (75% window, GUI hidden, preview on).
- OSC out: send `/presetX` when the transition first hits black (before load); UI lives under COLORS with enable toggle and shows host:port.
- Sequence bar: between COLORS and OSC OUT. Play/Stop + cycle progress, with sliders for cycle duration and trigger window. On trigger, randomly selects a preset and color.
- Preset particle transitions: particle systems store persistent IDs. When loading a preset, shared IDs continue live; missing systems fade out and stop spawning; new or returning systems spawn from zero and fade in.
- Sequence OSC: sends `/cueN` (1-20) five seconds before `/presetN` when a sequence-triggered preset change is scheduled. The last OSC address is shown under OSC OUT as `LAST /...`.
- Startup behavior: if preset 1 exists, it loads on launch and becomes the active preset; sequence only picks presets that have a saved file.
- Compact mode: key `P` toggles compact view (GUI hidden, window resized to output aspect at 75% height, preview only). In compact mode the progress bar appears near the FPS and the UI is non-interactive.

## Next Steps
- Scaffold folders and move `src/` to match the structure above.
- Implement `ParameterStore`, `NDIManager`, and `RenderGraph` stubs.
- Define OSC address map and DMX fixture JSON schema.
- Add a minimal diagnostics overlay and logging system.
