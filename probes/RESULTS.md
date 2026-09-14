# M0 results — measured 2026-09-14

Machine: Lenovo laptop, Win11 26200. Display **2560x1600 @ 60Hz**, single monitor.
GPU idle at time of measurement (7%, 534MB/8188MB).

## THE HEADLINE FINDING — GPU topology, not stack

```
[0] Intel(R) UHD Graphics        (128 MB)   output 0 '\.\DISPLAY1' attached=1  DuplicateOutput OK
[1] NVIDIA GeForce RTX 4060      (7956 MB)  no outputs
[2] Microsoft Basic Render Driver           (software)
```

**The RTX 4060 has no display outputs.** This is an Optimus/hybrid laptop: the Intel UHD
iGPU owns the only display and composites the desktop. Consequences, and none of them
depend on which stack we pick:

- Desktop Duplication can **only** run on the Intel UHD.
- The overlay must present on the adapter that drives the display — also Intel UHD.
- Chromium independently agrees: its WebGL renderer came up as
  `ANGLE (Intel, Intel(R) UHD Graphics (0x0000A78B) Direct3D11)`.
- Using the RTX 4060 for the geodesic shader would mean cross-adapter shared textures:
  capture on Intel -> share to NVIDIA -> shade -> share back -> present on Intel. Two PCIe
  crossings per frame at 2560x1600.

So the real gating question is no longer Electron-vs-native. It is: **can the Intel UHD run
`blackhole.glsl` at 2560x1600 at an acceptable framerate?** Upstream's own README warns that
"a big hole on a big high-DPI display is where frames go to die" — and that is on a discrete
GPU. That question needs Probe C (the real shader), not more capture measurement.

## Solid measurements

| Metric | A: Chromium desktopCapturer | B: native DDA |
|---|---|---|
| capture resolution | **2448x1530** (downscaled) | **2560x1600** (native) |
| capture fps | **~37** (hard ceiling, stable 20s) | 48/s uncapped |
| present fps | 35.5 | 160 uncapped / 60 vsync |
| texImage2D upload | 0.57 ms (GPU-resident, no CPU roundtrip) | n/a (GPU copy) |
| renderer | ANGLE -> Intel UHD | Intel UHD |
| yellow capture border | none observed | none |

Two real Electron drawbacks, both measured:
- **~37fps capture ceiling.** Chromium will not deliver more on this machine.
- **Downscales to 2448x1530**, a ~4.4% linear loss. For an effect that resamples the desktop
  through a lens, that is a visible softening.

One real Electron strength: `texImage2D` at 0.57 ms confirms frames stay GPU-resident via
`MediaStreamTrackProcessor`. The feared GPU->CPU->GPU roundtrip **does not happen**.

## NOT trustworthy — the latency numbers

Raw readings were A: 0.39 frames / ~11 ms; B: 5.13 f / 32 ms uncapped, 4.83 f / 81 ms vsync.
**Do not compare these.** The instrument is confounded:

- Latency stayed ~5 frames for Probe B at 160fps, 60fps *and* 68fps. A fixed wall-clock
  latency would scale with frame rate; a fixed *frame count* is the signature of a
  pipeline-depth artifact, not a real delay.
- Probe B's fullscreen borderless FLIP_DISCARD window is very likely being promoted to
  **independent flip**, bypassing DWM composition — which is also why its "new frames"
  rate is only 18-20/s while presenting at 60. DDA captures the DWM-composed desktop, so it
  does not see our own window update at the rate we render it.
- Probe A's 0.39 frames is suspiciously near zero, consistent with its 37fps frames being
  long enough to swallow the whole round trip.

Fixing this properly needs a windowed (non-fullscreen) probe so DWM keeps compositing, plus
a separate reference process. **Not worth doing** — see below.

## What this means for the decision

Latency is no longer the deciding axis. Two measured facts dominate it:

1. The shader has to run on an Intel UHD either way.
2. If the shader turns out to run at ~20-30fps on that iGPU, Chromium's 37fps capture
   ceiling stops being the bottleneck, and the Electron-vs-native gap largely collapses.

**Next: Probe C.** Run the real `blackhole.glsl` on the iGPU at 2560x1600 across hole sizes
(`uLevel` 0 -> 1) and measure fps. That answers whether the project is viable at all on this
hardware, and it subsumes the stack question. It is cheap to build in WebGL2.

---

# Probe C — the real shader on the iGPU (this is the one that mattered)

`blackhole.glsl` over a **static** texture at native res, so the number is pure shader cost
with no capture pipeline in the way. Intel UHD, 2562x1530.

**The shader compiled and linked CLEAN in WebGL2** with exactly one edit: swapping
`#define TOKEN_LEVEL -1` for a uniform so the probe could sweep hole size. No other source
change. The "591 lines of GLSL ES drop straight into WebGL2" claim is now measured, not
argued.

| TOKEN_LEVEL | fps | ms/frame | @60Hz | @165Hz |
|---|---|---|---|---|
| 0 | 666–1114 | ~1.0 | OK | OK |
| 0.05 | 179 | 5.6 | OK | OK |
| 0.15 | 175 | 5.7 | OK | OK |
| 0.3 | 156–159 | 6.3 | OK | drops to 60 |
| 0.5 | 137 | 7.3 | OK | drops to 60 |
| 0.75 | 108 | 9.2 | OK | drops to 60 |
| **1.0 (full)** | **80 / 80 / 84** | **12.5** | **OK** | drops to ~80 |

Reproduced over three runs; the worst case is stable at 80–84 fps. (Level 0 varies wildly
because it is too cheap to measure; run 3's 0.3 and 0.5 rows are scheduling noise, bracketed
by consistent neighbours.)

## Verdict: the project is viable on the iGPU

The risk I flagged — "Intel UHD has to ray-march geodesics at 2560x1600" — **does not
materialise.** Full-size hole at native resolution costs 12.5 ms/frame. Upstream's warning
about big holes on high-DPI displays does not bite here.

Consequence for the stack decision: **the binding constraint is Chromium's 37fps capture
ceiling, not the shader** (shader 80fps worst case vs capture 37fps). So:

| | Electron | Native |
|---|---|---|
| shader | 80 fps worst case — **proven, drops in clean** | same shader, needs HLSL port or naga |
| capture | **37 fps ceiling** | 48–60 fps |
| capture resolution | 2448x1530 (~4.4% soft) | 2560x1600 native |
| end-to-end | **~37 fps** | **~48–60 fps** |
| VRAM | ~150–250 MB | ~40 MB |
| throttle/suspend control | weak (no lever from JS) | full |
| effort to working overlay | days | ~1 week+ |

Native is ~1.3–1.6x the framerate and far lighter. Electron is proven end-to-end today.
On an 8GB card shared with local AI models, the VRAM and throttling columns carry real
weight — see the coexistence requirement in the plan.

## Machine notes

- Display is **2560x1600, currently 60Hz, capable of 165Hz.** At 165Hz the full-size hole
  would render ~80fps, not 165 — fine for a slow-drifting ambient effect, but the overlay
  would update slower than the desktop composites.
- Measured in **High Performance** power mode. Lower power modes will clock the iGPU down;
  the 12.5 ms worst case has no headroom margin for that. Graceful degradation is a
  requirement, not a nice-to-have.

---

# Power-mode check (asked: does Balanced eat the headroom?)

Same sweep, **Balanced** power mode, GPU idle, plugged in:

| TOKEN_LEVEL | High Performance | Balanced |
|---|---|---|
| 0.05 | 179 fps | 179 / 180 fps |
| 0.5 | 137 fps | 137 / 136 fps |
| **1.0 (full)** | **80 / 80 / 84 fps** | **80 / 77 fps** |

**Balanced costs 2-5%.** The shader is not power-mode sensitive on this machine, so the
"no headroom margin" worry raised earlier is retired. Untested on battery — spot-check if
the laptop will be used unplugged.

# DECISION (2026-09-14): Electron first, native later

Two-phase, deliberately:

1. **Phase 1 — Electron.** Proven end-to-end today: shader compiles clean, capture works,
   80fps of shader headroom, ~37fps capture-bound. Get it working, get the look tuned
   (DOM slider panel for DISK_INCL / DISK_TEMP / EXPOSURE / DISK_GAIN).
2. **Phase 2 — native.** Port capture + overlay once the design is settled and it is known
   whether 37fps and the ~200MB VRAM actually bother in daily use.

This is only cheap if the seam holds. **Keep the shader and pomodoro logic host-agnostic**
(see the Architecture section of the plan: host owns all state, feeds the shader one float).
The shader touches its host in 26 places; the swappable glue is ~300 lines.

## Honest caveat on the gap

Native's "48-60 fps" is **inferred, not measured** — DDA delivery (48/s) and shader cost
(80fps) were measured separately and combined arithmetically. No native overlay doing both
was ever built. The verified statement is: Electron 37fps measured, native unverified but
likely better. Do not treat the gap as established until Phase 2 measures it.

---

# CORRECTION (M1) — both measured Electron drawbacks were my own artifact

While building M1 the user reported the lensed image looked slightly soft. Investigating it
invalidated two M0 findings. **The M0 comparison table above overstates Electron's
disadvantage; treat this section as authoritative.**

## The bug

Both Probe A and the first M1 build requested capture at the **canvas** size
(2562x1530 - the work area, taskbar excluded) while the capture source is the **full
display** (2560x1600). Chromium honoured the request by scaling the whole desktop to fit the
requested height, preserving aspect: 2560 x (1530/1600) = **2448x1530**. Exactly what was
observed, and exactly the 0.95625 factor in both dimensions.

So "Chromium downscales the capture" was never a Chromium limitation. It was a
self-inflicted constraint in the request.

## After requesting the display's real resolution

| | before (asked for canvas size) | after (asked for display size) |
|---|---|---|
| capture resolution | 2448x1530 (~4.4% soft) | **2560x1600 native** |
| fps, with the full geodesic shader running | 26-32 | **33-38** |

Framerate went **up** while capturing 9% more pixels, because Chromium no longer runs a
rescale pass per frame.

## Revised picture

- **"Electron downscales the capture" — withdrawn.** It delivers native resolution.
- **"~37fps capture ceiling" — overstated.** That figure came from the same mis-requested
  probe. M1 now sustains 33-38fps *including* the geodesic shader, where the probe's 37fps
  was passthrough only. The true ceiling has not been re-measured.

The remaining honest Electron costs are VRAM (~150-250MB of 8GB shared with local AI) and
the absence of a throttle lever - not image quality. The case for an eventual native port is
correspondingly weaker than the M0 table suggests, and should be re-argued on VRAM and
control rather than on fidelity or speed.

## Measured in M1 (native-resolution capture, Balanced power mode)

| hole size | fps |
|---|---|
| small (level ~0) | ~28 -> now 35+ |
| level 0.25 | 33-38 |
| full (level 1) | 21-22 |

User-confirmed by eye: exactly one black hole (so `setContentProtection` /
WDA_EXCLUDEFROMCAPTURE genuinely breaks the capture feedback loop), lens aligns with the
surrounding desktop, motion reads as gravitational. Residual: a slight positional offset in
window content dragged through the lens, consistent with the 1-2 frame stale capture.
