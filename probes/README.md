# Milestone 0 — head-to-head capture measurement

**THROWAWAY.** These two probes exist only to pick the stack. Neither is kept.

Both run a *passthrough* shader, not the geodesic one — this measures the capture path,
nothing else.

## Run them on an idle GPU

The entire output is fps and latency. Measured while a local inference job saturates the
GPU, the numbers are meaningless and would pick the wrong stack. Each probe needs
~300–500MB VRAM at 4K, enough to OOM a benchmark sitting near the VRAM ceiling.

## How latency is measured

Neither probe excludes itself from capture. Each draws an 8-bit counter as black/white
blocks in the top-left of the screen, then decodes that counter back out of its *own
capture*. The difference between the counter it is drawing now and the counter it can see
in the capture **is** the end-to-end latency, in frames. No clock sync, no second process.

Readback happens every 10th frame — `readPixels`/`Map` stall the pipeline, and sampling
sparsely keeps that from contaminating the fps number.

## Running

    # Probe A — Chromium desktopCapturer (default path: DXGI)
    cd probes/electron && npm start

    # Probe A — same, but forcing Chromium onto Windows.Graphics.Capture
    cd probes/electron && set WGC=1 && npm start

    # Probe B — native Desktop Duplication ceiling
    cd probes/native && ./probeB.exe

Esc quits either. Let each settle ~15s before reading numbers. Run them **one at a time** —
two capture pipelines at once will contend and skew both.

While each is running, also note from Task Manager (Details → GPU / Dedicated GPU memory):
idle **GPU %**, **VRAM**, **RAM**.

## Record results here

| Metric | A: Chromium (DXGI) | A: Chromium (WGC) | B: native DDA |
|---|---|---|---|
| present fps | | | |
| capture fps / new frames / s | | | |
| **latency (frames)** | | | |
| **latency (ms)** | | | |
| texImage2D / copy (ms) | | | n/a |
| capture resolution | | | |
| yellow capture border? | | | |
| GPU % | | | |
| VRAM | | | |
| RAM | | | |

## Decision rule

Read the **gap**, not a threshold. Electron wins unless it is materially worse on latency
or framerate, because the shader port is near-free there (591 lines of GLSL ES, no
`#version`/`#extension`/exotic features) and shader tuning gets a DOM slider panel.

- **Gap small** → Electron + WebGL2. Accept ~150–250MB and the always-on GPU process.
- **Gap large** → native. Then settle C++ vs Rust: `wgpu`+`naga` ingests the GLSL directly
  (no HLSL port) but has awkward DirectComposition interop; C++/D3D11+DComp is the
  well-trodden overlay path but needs the shader ported to HLSL.

## Toolchain (verified present, nothing to install)

- node v22.15.1, npm 11.19.0, electron 33.4.11 (installed)
- MSVC 19.50.35723 (VS 18 Community) + Windows SDK 10.0.26100.0
- `probes/native/build.bat` activates vcvars64 and compiles `probeB.exe`
