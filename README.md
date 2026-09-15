# Black Hole Pomodoro

A ray-traced black hole that floats over your Windows desktop and gravitationally lenses
your actual windows — text bends around it, mirrors inside the Einstein ring, and
disappears behind the event horizon. It grows through a pomodoro focus block and collapses
when it is time to stop.

Windows port of [s0xDk/ghostty-blackhole](https://github.com/s0xDk/ghostty-blackhole), which
does this inside the Ghostty terminal. Here the whole desktop is the lensed sky.

This is the **native** build: C++ / Direct3D 11 / Desktop Duplication / DirectComposition,
no Electron at runtime. It replaced the Electron version because the lens trailed dragged
windows by 3–4 frames there and nothing in JS could shorten Chromium's capture pipeline;
here it is one frame. The Electron sources are kept under `src/` only for the shader-port
check (see *Development*).

## Running

```
native\build.bat          # needs Visual Studio 2022+ with the Windows 10/11 SDK
native\BlackHolePomodoro.exe
```

`run.bat` builds if needed and launches; `Black Hole Pomodoro.vbs` launches with no console.
Requires Windows 10 2004+ (build 19041) — see *How it works*. Tested on Windows 11.

## Using it

Everything lives in the tray icon. The overlay itself is click-through, so you cannot click
it; the tray and the hotkeys are the entire control surface.

### Three modes

**Free** — ambient. The hole is whatever size you set and stays there. Nothing is timed.

**Pomodoro** — the clock owns the size. It grows through a 55-minute focus block, collapses
over the final minute (*that collapse is the signal to stop*), and vanishes for a 5-minute
break before starting over.

**Eye breaks** — the 20-20-20 rule. Every 20 minutes the hole swallows the screen for 20
seconds so you actually look away. The whole cycle is a ramp, so the hole's size *is* how
close the next break is: it starts small, barely grows for most of the interval, climbs hard
over the last few minutes to *Size before a break*, engulfs the screen, holds, then recedes
gently back to small. The four phases join continuously — no jumps.

It never shrinks to *nothing*: a hole that vanishes for minutes reads as the app having
died, and switching into the mode would answer the keypress with an empty screen. The floor
scales with *Size before a break*, so choosing **Hidden** still hides it completely.

### Tray

| | |
|---|---|
| **Hide everything** | keep running, draw nothing, stop capturing |
| **Size** | Hidden / Small / Medium / Large / Full. In eye-break mode this is *Size before a break* — what the ramp climbs to, not a constant size |
| **Shrink back** | eye breaks: how gradually it recedes afterwards |
| **Movement** | Wander across all monitors, or keep it still at Centre / Top left / Top right / Upper centre / the cursor |
| **Drift speed** | Frozen / Slow / Normal / Fast — how fast it wanders |
| **Growth** | Steady / Late surge / Dramatic — how size ramps through a focus block |
| **Look** | 7 presets: inferno, gargantua, m87\* donut, face-on ember, quasar, blazar, pure lens |
| **Fade when I am away** | fade out after 90s of inactivity |
| **Show HUD** | live stats in the corner |
| **Start with Windows** | registers a Run key for this exe |

**Hide everything** is the "leave it running but shut up" switch. It releases the capture,
blanks every screen and parks the render thread, so a hidden overlay costs nothing. The
tray and hotkeys stay live.

`Growth` is worth trying. On **Late surge** (the default) a 55-minute block sits at 8% after
20 minutes and 23% after 30, then climbs hard over the last 15 — it leaves you alone through
the bulk of the session instead of looming the whole way.

### Hotkeys

| | |
|---|---|
| `Ctrl+Alt+Up` / `Down` | grow / shrink |
| `Ctrl+Alt+0` / `1` | hidden / full |
| `Ctrl+Alt+P` | switch mode |
| `Ctrl+Alt+S` | start / pause the clock |
| `Ctrl+Alt+B` | take an eye break now |
| `Ctrl+Alt+K` | toggle pinned at cursor / wandering |
| `Ctrl+Alt+X` | **hide / show everything** |
| `Ctrl+Alt+H` | HUD |
| `Ctrl+Alt+Q` | **quit** |

Settings persist to `%APPDATA%\blackhole-pomodoro\config.json` — the same file and schema
as the Electron build, so they carry over.

## How it works

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

`shader/blackhole.glsl` is upstream's file, vendored unmodified. `src/shader-patch.js`
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

## Known limitations

- **The mouse cursor is not lensed.** It is composited by hardware above everything.
- **Content dragged through the lens can still be one frame behind.** Inherent to capturing
  the screen and drawing on top of it; a drag-time softening is the obvious next step.
- **Exclusive-fullscreen apps** (some games) bypass DWM, so the overlay will not draw over
  them. Borderless fullscreen is fine.
- **HDR / FP16 desktops** are untested; the swapchain and capture are BGRA8.
- **The hole is sized per screen, not per desktop.** Its size is a fraction of the screen's
  *area*, so on monitors with different pixel counts it is physically different — 81 px vs
  57 px shadow radius across the pair here — and the two halves do not line up while it
  straddles the seam.
- **EDR / anti-cheat.** A topmost, click-through, capture-excluded window that continuously
  reads the desktop looks exactly like a cheat overlay. Fine personally; expect friction on
  a managed machine.

## Development

```
native\build.bat                                        # build
set FAST=1& native\BlackHolePomodoro.exe                # a full pomodoro cycle in ~40s
set IDLE_AFTER=5& native\BlackHolePomodoro.exe          # idle fade after 5s instead of 90
set SMOKE=10& native\BlackHolePomodoro.exe              # run 10s, log to smoke.txt, quit
set TOGGLE_HIDE=5& native\BlackHolePomodoro.exe         # hide at 5s, unhide at 10s
set RESTART_AT=5& native\BlackHolePomodoro.exe          # force the resume/unlock capture restart
node_modules\.bin\electron tools\still\run-still.js     # shader port check (needs npm install)
node tools\gen-presets.js                               # regenerate presets.gen.h after a shader update
native\test\run.bat                                     # unit-check the pomodoro / eye-break clocks
```

`eyebreak.growthCurve` in `config.json` shapes the ramp (1 = a straight line, 3 = the
default late surge, higher = flatter for longer then steeper).

The env vars force settings, so they never write to your real config. `SMOKE` also logs
per-second render stats and how the first frames were classified (real vs echo).

`probes/` holds the throwaway measurements that chose the original stack; `probes/RESULTS.md`
is the record.

## Credits

Shader by [s0xDk](https://github.com/s0xDk/ghostty-blackhole) (MIT, vendored in `shader/`
with its licence), after
[Eric Bruneton's black hole shader](https://ebruneton.github.io/black_hole_shader/).
