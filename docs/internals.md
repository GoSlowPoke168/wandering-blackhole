# Internals

How *Wandering Black Hole* works, and the things that were only learned by getting them
wrong. None of this is needed to use the app — see the [README](../README.md) for that.

## Why native

The first build was Electron. The lens trailed dragged windows by 3-4 frames, because the
capture path was webrtc capturer -> CPU copy -> MediaStream -> VideoFrame -> texImage2D ->
Chromium compositor -> DWM, and nothing in JS could shorten it. The C++ build is one frame.

## The overlay

| | |
|---|---|
| Desktop → GPU texture | DXGI Desktop Duplication, one `CopyResource` per changed frame |
| Click-through overlay with per-pixel alpha | full-monitor `WS_POPUP` window, `WS_EX_NOREDIRECTIONBITMAP \| TRANSPARENT \| TOPMOST \| NOACTIVATE`, DirectComposition visual over a premultiplied flip swapchain |
| **Not capturing our own output** | `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` |

That third row is the one the whole thing rests on. DWM renders the overlay to the physical
display but omits it from *every* capture pipeline — without it we capture our own output
and the desktop recurses into itself forever. It needs Windows 10 2004+.

### The frame loop

- **Latency.** The render thread wakes on the compositor clock
  (`DCompositionWaitForCompositorClock`), polls the duplication with a zero timeout, and
  presents the lensed frame before the next vblank. Measured 0–1 capture frames behind
  the desktop (the Electron build was 3–4).
- **Only what changed.** The shader runs inside a scissor rect around the lens (its reach
  is 7–14 shadow radii, measured by `tools/still`), and the present carries a dirty rect,
  so DWM recomposes only that region. A still hole over a still desktop draws nothing.
- **Echo frames.** We are excluded from the capture *image* but not from DWM's damage
  tracking, so each of our own presents comes back through the duplication as a "new"
  frame. A frame whose dirty rects all sit inside what we last presented is an echo:
  copied, but not a reason to redraw.
- **Pacing pitfall.** Never block on the swapchain's frame-latency waitable: on a static
  desktop DWM consumes small presents lazily (25–50 ms) and the loop drops to 20 fps. Poll
  it, and keep a change pending if the previous present has not been consumed.
- **Recovery.** Sleep, the lock screen and mode changes invalidate the duplication
  (`DXGI_ERROR_ACCESS_LOST`); the overlay blanks — never a stale frame — and retries every
  500 ms. Resume and unlock also force a restart. Display changes rebuild every overlay
  after a short debounce.

### The shader is patched, then ported

`shader/blackhole.glsl` is upstream's file, vendored unmodified. `tools/shader-patch.js`
rewrites it in memory (26 tunables → uniforms, transparency, desktop UV remap, host-owned
drift clock, position override — see the comments there), and `native/shader/blackhole.hlsl`
is a hand port of *that* output with the Ghostty-only paths removed (cursor-token decode,
demo tour, wall-clock pomodoro). `tools/still` renders the same cases through both and
diffs them: mean error 0.000/255, max 1/255.

`tools/gen-presets.js` harvests the defaults and the look presets out of the GLSL into
`native/src/presets.gen.h`, so upstream stays the single source of truth.

### Hybrid GPUs

On an Optimus laptop the iGPU owns the panel and Desktop Duplication only works on that
adapter. NVIDIA's driver profiles match on **exe name**, and a generic name (the first
spike was `overlay.exe`) got the process forced onto the dGPU, where DXGI remapped the
output and `DuplicateOutput` failed. The app checks `QueryDisplayConfig` against the
output's DXGI adapter at start; on a mismatch it writes the per-app GPU preference for this
exe and relaunches once.

### Multiple monitors

All displays are treated as **one continuous canvas**: the hole has a single position in
"virtual uv" across the bounding box of every display, one overlay window and one
duplication per output, and the host owns the position. State reaches the render thread
as a snapshot carrying the drift *clock*, not the position, so the wander is evaluated per
frame and never steps.

The shader keeps the bottom of each screen undistorted, and **how much is measured per
monitor** (`MONITORINFO`'s work area) rather than taken as upstream's flat third. That third
was a *terminal's* work area; applied to every window it killed the warp over the bottom
half of a shorter second monitor — the virtual desktop is as tall as the tallest screen, so
a hole at mid-height on a 1600-tall desktop lands 74% of the way down a 1080-tall one. Now
only the taskbar strip stays clear (~0.05 of each screen here), and warping survives the
whole roam on both.

The hole is sized as a fraction of each screen's **area**, not in pixels, so it keeps its
physical size across screens of different resolution: measured across this pair, 81 px on a
191 PPI laptop panel and 57 px on a 135 PPI external — 10.8 mm and 10.7 mm on the glass. It
would only differ on screens of genuinely different physical area.

Whether the lens reaches a given window is decided by its **clamped scissor rect**, never by
a margin guess. Those two disagreed at first: `shouldShade()` allowed 0.35 of the window
width while a small hole's lens only reached 0.18, so a hole sitting on the *other* monitor
produced an inverted rect, `Present1` rejected the empty dirty rect with
`DXGI_ERROR_INVALID_CALL`, and the unreturned frame-latency slot wedged that swapchain for
good. Verified across a full wander sweep over two screens at small size.

## Performance

Measured on the same laptop (Intel UHD drives a 2560×1600 165 Hz panel; RTX 4060 has no
outputs), level 0.15, m87\* donut, static desktop, Balanced power mode:

| | Electron | Native |
|---|---|---|
| capture latency | 3–4 frames | 0–1 frame |
| fps | ~40 (capture-bound) | 60 (animation clock; 165 Hz compositor wake) |
| GPU 3D engine | 65% | 40% while wandering, ~0 when still |
| shared GPU memory | 323 MB | 73 MB |
| working set | 891 MB (4 processes) | 65 MB |
| CPU | — | ~0.3% of all cores |

## Development

```
native\build.bat                                     # build
set FAST=1& native\WanderingBlackHole.exe            # a full pomodoro cycle in ~40s
set IDLE_AFTER=5& native\WanderingBlackHole.exe      # idle fade after 5s instead of 90
set SMOKE=10& native\WanderingBlackHole.exe          # run 10s, log to smoke.txt, quit
set TOGGLE_HIDE=5& native\WanderingBlackHole.exe     # hide at 5s, unhide at 10s
set RESTART_AT=5& native\WanderingBlackHole.exe      # force the resume/unlock capture restart
set BHP_CAPTURABLE=1& native\WanderingBlackHole.exe  # drop the capture exclusion, to screenshot the HUD
node_modules\.bin\electron tools\still\run-still.js  # shader port check (needs npm install)
node tools\gen-presets.js                            # regenerate presets.gen.h after a shader update
native\test\run.bat                                  # unit-check the pomodoro / eye-break clocks
```

The env vars force settings, so they never write to your real config. `SMOKE` also logs
per-second render stats and how the first frames were classified (real vs echo).

`BHP_CAPTURABLE` exists only for working on the HUD: without the exclusion the lens captures
its own output and the desktop recurses into itself, so never leave it set.

`eyebreak.growthCurve` in `config.json` shapes the ramp (1 = a straight line, 3 = the
default late surge, higher = flatter for longer then steeper).

`probes/` keeps the write-up of the throwaway measurements that chose this stack; the probe
code itself is gone, in git history.
