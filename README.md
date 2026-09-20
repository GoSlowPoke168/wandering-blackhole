# Wandering Black Hole

A ray-traced black hole that floats over your Windows desktop and gravitationally lenses
your actual windows — text bends around it, mirrors inside the Einstein ring, and
disappears behind the event horizon. It grows through a pomodoro focus block and collapses
when it is time to stop.

Windows port of [s0xDk/ghostty-blackhole](https://github.com/s0xDk/ghostty-blackhole), which
does this inside the Ghostty terminal. Here the whole desktop is the lensed sky.

## Install

Download `WanderingBlackHole.exe` from [Releases](../../releases) and run it — one file, no
installer. Needs Windows 10 2004+ (build 19041) or Windows 11.

It is unsigned, so SmartScreen shows *"Windows protected your PC"* on first run: **More info
→ Run anyway**. [What it does to your system](#what-it-does-to-your-system) says exactly what
it touches, which is worth reading first.

To build it instead, with Visual Studio and the C++ desktop workload:

```
native\build.bat
native\WanderingBlackHole.exe
```

`run.bat` builds if needed and launches; `Wandering Black Hole.vbs` launches with no console.

## Using it

Everything lives in the tray icon. The overlay is click-through, so you cannot click it — the
tray and the hotkeys are the entire control surface.

### Three modes

**Free** — ambient. The hole is whatever size you set and stays there. Nothing is timed.

**Pomodoro** — the clock owns the size. It grows through a 55-minute focus block, collapses
over the final minute (*that collapse is the signal to stop*), and vanishes for a 5-minute
break before starting over.

**Eye breaks** — the 20-20-20 rule. Every 20 minutes the hole swallows the screen for 20
seconds so you actually look away. Its size *is* how close the next break is: small for most
of the interval, climbing hard over the last few minutes, then engulfing the screen and
receding. It never shrinks to nothing, since a hole that vanishes for minutes reads as the
app having died — choose **Hidden** if you want it gone.

### Hotkeys

| | |
|---|---|
| `Ctrl+Alt+Up` / `Down` | grow / shrink |
| `Ctrl+Alt+0` / `1` | hidden / full |
| `Ctrl+Alt+P` | switch mode |
| `Ctrl+Alt+S` | start / pause the clock |
| `Ctrl+Alt+B` | take an eye break now |
| `Ctrl+Alt+K` | toggle pinned at cursor / wandering |
| `Ctrl+Alt+.` | **pause / resume** |
| `Ctrl+Alt+X` | **hide / show everything** (also pauses) |
| `Ctrl+Alt+H` | HUD |
| `Ctrl+Alt+Q` | **quit** |

### Tray

| | |
|---|---|
| **Pause** | freeze the drift, the disk and the clocks; the hole stays where it is |
| **Hide and pause** | keep running, draw nothing, stop capturing — hiding always pauses too |
| **Size** | Hidden / Small / Medium / Large / Full. In eye-break mode this is *Size before a break* — what the ramp climbs to, not a constant size |
| **Shrink back** | eye breaks: how gradually it recedes afterwards |
| **Movement** | Wander across all monitors, or keep it still at Centre / Top left / Top right / Upper centre / the cursor |
| **Drift speed** | Frozen / Slow / Normal / Fast — how fast it wanders |
| **Growth** | Steady / Late surge / Dramatic — how size ramps through a focus block |
| **Look** | 7 presets: inferno, gargantua, m87\* donut, face-on ember, quasar, blazar, pure lens |
| **Fade when I am away** | fade out after 90s of inactivity |
| **Show HUD** | live stats in the corner |
| **HUD background** | None / Faint / Dim / Shaded / Dark / Solid — how much of the desktop the readout hides |
| **Start with Windows** | registers a Run key for this exe |

**Hide and pause** is the "leave it running but shut up" switch: it releases the capture and
blanks every screen, so a hidden overlay costs nothing, while the tray and hotkeys stay live.
**Pause** keeps the hole on screen but freezes the wander, the accretion disk and both
timers, so nothing changes size behind your back.

**Growth** is worth trying. On **Late surge** (the default) a 55-minute block sits at 8%
after 20 minutes and 23% after 30, then climbs hard over the last 15 — it leaves you alone
through the bulk of the session instead of looming the whole way.

`Ctrl+Alt+H` shows the HUD: mode and state, current and target fill, shadow radius, screen
count, presence and frame cost. **HUD background** sets how much of the desktop it hides;
`hudOpacity` in `config.json` takes any value between the presets.

Settings persist to `%APPDATA%\wandering-blackhole\config.json`, written atomically.

## What it does to your system

Worth reading before you run a build you did not compile yourself: an always-on program
that reads the screen and hides itself from capture is also a fair description of spyware.
So, precisely:

- It captures the screen continuously through Desktop Duplication. That is how it lenses
  the windows behind it, and it is the whole reason the app exists.
- It sets `WDA_EXCLUDEFROMCAPTURE` on its own windows so it never recurses into its own
  output. The side effect is that it is invisible in screenshots and screen shares.
- It has no network code at all. Nothing leaves the machine.
- It registers 14 global `Ctrl+Alt` hotkeys.
- It writes two `HKCU` values: the `CurrentVersion\Run` autostart entry, only when you turn
  autostart on, and a per-app GPU preference on hybrid laptops so Desktop Duplication lands
  on the adapter driving the panel.
- It stores settings in `%APPDATA%\wandering-blackhole\config.json` and nothing else.

Release binaries are built by GitHub Actions from a tag and carry a provenance attestation
you can check with `gh attestation verify`; GitHub shows each asset's own SHA-256.

## Known limitations

- **The mouse cursor is not lensed.** It is composited by hardware above everything.
- **Content dragged through the lens can still be one frame behind.** Inherent to capturing
  the screen and drawing on top of it.
- **Exclusive-fullscreen apps** (some games) bypass DWM, so the overlay will not draw over
  them. Borderless fullscreen is fine.
- **HDR / FP16 desktops** are untested; the swapchain and capture are BGRA8.
- **EDR / anti-cheat.** A topmost, click-through, capture-excluded window that continuously
  reads the desktop looks exactly like a cheat overlay. Fine personally; expect friction on
  a managed machine.

## Development

[`docs/internals.md`](docs/internals.md) covers how it works — the frame loop, the shader port, hybrid GPUs and
multi-monitor — along with the build and test commands.

## Credits

Shader by [s0xDk](https://github.com/s0xDk/ghostty-blackhole) (MIT, vendored in `shader/`
with its licence), after
[Eric Bruneton's black hole shader](https://ebruneton.github.io/black_hole_shader/).

MIT, like the shader it is built on — see `LICENSE`.
