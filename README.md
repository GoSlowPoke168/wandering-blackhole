# Black Hole Pomodoro

A ray-traced black hole that floats over your Windows desktop and gravitationally lenses
your actual windows — text bends around it, mirrors inside the Einstein ring, and
disappears behind the event horizon. It grows through a pomodoro focus block and collapses
when it is time to stop.

Windows port of [s0xDk/ghostty-blackhole](https://github.com/s0xDk/ghostty-blackhole), which
does this inside the Ghostty terminal. Here the whole desktop is the lensed sky.

## Running

```
npm install
npm start
```

Requires Windows 10 2004+ (build 19041) — see *How it works*. Tested on Windows 11.

## Using it

Everything lives in the tray icon. The overlay itself is click-through, so you cannot click
it; the tray and the hotkeys are the entire control surface.

### Two modes

**Free** — ambient. The hole is whatever size you set and stays there. Nothing is timed.

**Pomodoro** — the clock owns the size. It grows through a 55-minute focus block, collapses
over the final minute (*that collapse is the signal to stop*), and vanishes for a 5-minute
break before starting over.

### Tray

| | |
|---|---|
| **Hide everything** | keep running, draw nothing, stop capturing |
| **Size** | Hidden / Small / Medium / Large / Full (free mode only) |
| **Movement** | Wander across all monitors, or keep it still at Centre / Top left / Top right / Upper centre / the cursor |
| **Drift speed** | Frozen / Slow / Normal / Fast — how fast it wanders |
| **Growth** | Steady / Late surge / Dramatic — how size ramps through a focus block |
| **Look** | 7 presets: inferno, gargantua, m87\* donut, face-on ember, quasar, blazar, pure lens |
| **Fade when I am away** | fade out after 90s of inactivity |

**Hide everything** is the "leave it running but shut up" switch. It blanks every screen,
stops the capture streams, and drops the render loop from the vsync clock to 4 Hz, so a
hidden overlay costs essentially nothing. The tray and hotkeys stay live. Resuming takes
about 200 ms while capture restarts.

**Movement** either wanders or pins. Wander drifts on a Lissajous path that never repeats,
tuned slow — roughly a minute to cross at 1x. `Drift speed` scales that; `Frozen` stops it
where it stands.

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
| `Ctrl+Alt+K` | toggle pinned at cursor / wandering |
| `Ctrl+Alt+X` | **hide / show everything** |
| `Ctrl+Alt+H` | HUD |
| `Ctrl+Alt+Q` | **quit** |

Settings persist to `%APPDATA%\blackhole-pomodoro\config.json`.

## How it works

Three Windows APIs make it possible:

| | |
|---|---|
| Desktop → GPU texture | Chromium `desktopCapturer`, delivered as GPU-resident `VideoFrame`s |
| Click-through overlay with per-pixel alpha | transparent always-on-top `BrowserWindow` |
| **Not capturing our own output** | `setContentProtection(true)` = `SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE)` |

That third row is the one the whole thing rests on. DWM renders the overlay to the physical
display but omits it from *every* capture pipeline — without it we capture our own output
and the desktop recurses into itself forever. It needs Windows 10 2004+.

### The shader is patched, not forked

`shader/blackhole.glsl` is upstream's file, vendored unmodified. `src/shader-patch.js`
rewrites it in memory at load time, so upstream stays easy to update. Every patch asserts
that it applied, so an upstream change fails loudly instead of silently producing a subtly
wrong shader. Nine patches:

- 26 `const float` tunables promoted to uniforms, so the look is live-adjustable.
- `LOOK_DEFAULT` is a `const` struct built from those tunables, which becomes illegal GLSL
  the moment they are uniforms (globals need constant initialisers) — inlined at its single
  use site.
- **Transparency.** Ghostty composites over the terminal it already drew, so upstream emits
  the background verbatim wherever nothing is happening. An overlay cannot: that would paint
  a 1–2 frame stale copy over the live desktop and ghost whenever anything moves. Those
  paths become `alpha = 0`, and the wide weak-field region fades its alpha in with the
  deflection.
- **Desktop UV remap.** Windows clamps the overlay window to the work area (it stops above
  the taskbar) while the capture covers the full screen, so sampling has to be remapped or
  the lens shows a vertically squashed desktop that does not line up with the real one.
- **Host-owned drift clock.** Upstream warns in-file that rescaling its drift clock
  teleports the hole, because `iTime` is large and a changed multiplier jumps the phase. The
  host integrates its own clock instead, so speed changes stay continuous and 0 freezes.

The disk presets are harvested out of upstream's own `DEMO_TOUR` array rather than invented,
so they stay in sync with the shader.

### Multiple monitors

All displays are treated as **one continuous canvas**. The hole has a single position in
"virtual uv" - 0..1 across the bounding box of every display - and each overlay converts
that into its own canvas uv. A hole sitting on the seam renders as uv 1.0 on the left
screen and 0.0 on the right, so it straddles the gap continuously.

One window per display, each with its own capture stream. A single window spanning the
virtual desktop does not work: Windows clamps it, and Chromium captures per-display anyway.

Two details make it behave:

- **The host owns the position.** With a window per display, letting each shader compute
  its own roam would draw a *separate black hole on every screen*. So the position is
  always host-driven and `uCenterPin` is always 1.
- **State is pushed once a second, and does not contain the position.** At 1 Hz the wander
  would move in visible steps. Instead the state carries the drift *clock*, and every
  renderer evaluates the same deterministic wander function each frame - so all monitors
  agree exactly, with no per-frame IPC.

Monitors the hole is nowhere near skip shading entirely, so extra screens cost almost
nothing. Displays appearing or disappearing (docking, cable, resolution change) rebuild the
windows after a short debounce.

### Host owns the state

The renderer draws what it is told and decides nothing. Mode, size, position, drift and look
all live in the main process. That keeps the shader and the pomodoro logic independent of
Electron, so swapping the capture/overlay layer for a native one later is roughly 300 lines.

This is also what lets the pomodoro be a real per-streak stopwatch. Upstream's README settles
for "an hourly bell, not a per-streak stopwatch" because a Ghostty shader is stateless and
cannot remember when your streak began. A host process can.

## Performance

Measured on an RTX 4060 laptop at 2560×1600. Note the **RTX has no display outputs** — it is
a hybrid-graphics machine, so the Intel UHD iGPU owns the display and everything (capture,
shading, compositing) runs there.

| | |
|---|---|
| shader alone, full-size hole | 12.5 ms/frame (~80 fps) |
| end to end, capture + shading | 40–45 fps |
| Balanced vs High Performance power mode | 2–5% difference |

When the hole fades out, the renderer stops shading entirely rather than drawing an invisible
hole — an empty desk costs essentially nothing, which matters on a machine that also runs
local inference.

## Known limitations

- **The mouse cursor is not lensed.** It is composited by hardware above everything, so it
  floats over the shadow instead of bending. Not fixable without hiding the system cursor.
- **Content dragged through the lens lags slightly.** The capture is 1–2 frames behind, so
  the lens interior disagrees with the live desktop around it when things move fast. This is
  inherent — you cannot capture the screen and draw on top of it within the same frame. A
  native implementation would shrink it, not remove it.
- **Exclusive-fullscreen apps** (some games) bypass DWM, so the overlay will not draw over
  them. Borderless fullscreen is fine.
- **Multi-monitor is implemented but unverified.** It was written and tested against
  synthetic layouts (side by side, second screen on the left, stacked, three wide) but
  never against real hardware - the development machine has one display. The
  single-display path is verified.
- **EDR / anti-cheat.** A topmost, click-through, capture-excluded window that continuously
  reads the desktop looks exactly like a cheat overlay. Fine personally; expect friction on a
  managed machine.

## Development

```
npm start                 # normal
FAST=1 npm start          # a full pomodoro cycle in ~40s, to watch growth and collapse
IDLE_AFTER=5 npm start    # idle fade after 5s instead of 90
SMOKE=10 npm start        # run 10s, log to smoke.txt, quit
TOGGLE_HIDE=5 npm start   # hide at 5s, unhide at 10s, to exercise capture stop/restart
```

Those env vars force settings, so they never write to your real config.

`probes/` holds the throwaway measurements that chose the stack (Chromium capture vs native
Desktop Duplication vs shader cost on the iGPU); `probes/RESULTS.md` is the record, including
two findings that later turned out to be measurement bugs of mine.

## Credits

Shader by [s0xDk](https://github.com/s0xDk/ghostty-blackhole) (MIT, vendored in `shader/`
with its licence), after
[Eric Bruneton's black hole shader](https://ebruneton.github.io/black_hole_shader/).

## Gotchas found the hard way

- **Do not set `disable-frame-rate-limit`.** Rendering is driven by
  `requestAnimationFrame`; without the vsync cap it runs unthrottled (~1100 calls/sec),
  floods the GPU command queue and stalls rendering entirely after a second or two.
- **Do not gate rendering on new capture frames.** Chromium only delivers a frame when the
  screen changes, so a static desktop froze all animation — the hole stopped drifting and
  size changes did not appear until something else happened to redraw. Upload on new
  frames, draw every frame.
- **Request capture at the display resolution, not the canvas size.** The canvas covers only
  the work area; asking for its height makes Chromium scale the whole desktop down to fit,
  which reads as a soft lens. Asking for the real display size is both sharper and faster.
