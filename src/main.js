'use strict';
const { app, BrowserWindow, desktopCapturer, session, screen, globalShortcut,
        ipcMain, Tray, Menu, nativeImage, powerMonitor } = require('electron');
const path = require('path');

const { build, presets } = require('./shader-patch.js');
const { load, save } = require('./config.js');
const { Pomodoro, PHASE } = require('./pomodoro.js');
const { EyeBreak } = require('./eyebreak.js');
const { trayIcon } = require('./icon.js');
const G = require('./geometry.js');

// NOTE: do not add 'disable-frame-rate-limit'. Rendering is driven by
// requestAnimationFrame, so removing the vsync cap makes it run unthrottled (~1100
// calls/sec), floods the GPU command queue and stalls rendering entirely after a
// second or two. Vsync pacing is what we want.

let tray = null, cfg = null, clock = null, eyes = null, LOOKS = [], CONFIG_FILE = null;
let overlays = new Map();        // displayId -> { win, bounds, scale }
let driftTime = 0;               // host-owned, so every monitor agrees on the position
let lastTick = Date.now();

const TEST_RUN = !!(process.env.FAST || process.env.IDLE_AFTER || process.env.SMOKE);

// ------------------------------------------------------------- overlays -----
// One window per display. The alternative - a single window spanning the virtual
// desktop - does not work: Windows clamps it, and Chromium captures per-display
// anyway, so each screen needs its own capture stream regardless.
function buildOverlays() {
  for (const { win } of overlays.values()) if (!win.isDestroyed()) win.destroy();
  overlays.clear();

  for (const d of screen.getAllDisplays()) {
    const b = d.bounds;
    const win = new BrowserWindow({
      x: b.x, y: b.y, width: b.width, height: b.height,
      transparent: true, frame: false, resizable: false, movable: false,
      minimizable: false, maximizable: false, focusable: false, skipTaskbar: true,
      hasShadow: false,
      webPreferences: { nodeIntegration: true, contextIsolation: false, backgroundThrottling: false },
    });
    win.setAlwaysOnTop(true, 'screen-saver');
    win.setVisibleOnAllWorkspaces(true, { visibleOnFullScreen: true });
    win.setIgnoreMouseEvents(true, { forward: true });

    // THE critical call. On Windows 10 2004+ this is SetWindowDisplayAffinity with
    // WDA_EXCLUDEFROMCAPTURE: DWM renders the window to the physical display but omits
    // it from every capture pipeline. Without it each overlay captures its own output
    // and the desktop recurses into itself.
    win.setContentProtection(true);

    const entry = { win, displayId: d.id, bounds: b, scale: d.scaleFactor };
    overlays.set(d.id, entry);

    win.webContents.on('did-finish-load', () => {
      win.webContents.send('setup', {
        displayId: d.id,
        display: Object.assign({}, b, { scale: d.scaleFactor }),
        window: win.getBounds(),
      });
      pushState();
    });
    win.loadFile(path.join(__dirname, 'index.html'));
  }
}

// Displays come and go (docking, cables, resolution changes). Rebuild rather than try
// to patch windows in place - it is rare and cheap, and half-updated geometry would
// put the hole in the wrong place.
let rebuildTimer = null;
function scheduleRebuild() {
  clearTimeout(rebuildTimer);
  rebuildTimer = setTimeout(() => { buildOverlays(); refreshTray(); }, 400);
}

// ---------------------------------------------------------------- state -----
function idleFactor() {
  if (!cfg.idle.enabled) return 1;
  let idle = 0;
  try { idle = powerMonitor.getSystemIdleTime(); } catch (e) { return 1; }
  const over = idle - cfg.idle.afterSec;
  if (over <= 0) return 1;
  return Math.max(0, 1 - over / Math.max(1, cfg.idle.fadeSec));
}

function currentLevel() {
  if (cfg.hidden) return 0;
  let base;
  if (cfg.mode === 'pomodoro') base = clock.level();
  else if (cfg.mode === 'eyebreak') {
    // While waiting the hole just sits at the resting size the user picked; during a
    // break the clock takes over and swells it to swallow the screen.
    const o = eyes.level(cfg.free.level);
    base = o === null ? cfg.free.level : o;
  } else base = cfg.free.level;
  // An eye break must not be faded out by the idle timer - being about to be told to
  // look away is not the same as being away.
  const idleSafe = (cfg.mode === 'eyebreak' && eyes.inBreak()) ? 1 : idleFactor();
  return base * idleSafe;
}

// The host owns the hole's position for every monitor. It has to: with one window per
// display, letting each shader compute its own roam would draw a separate black hole
// on every screen.
function holePosition(level) {
  const virt = G.virtualBounds(screen.getAllDisplays());
  const base = cfg.free.motion === 'still'
    ? cfg.free.center
    : G.wanderUV(driftTime, level, virt.width / Math.max(1, virt.height));
  // During an eye break, slide to the middle so it engulfs evenly rather than
  // swallowing the screen lopsidedly from wherever it happened to be.
  if (cfg.mode === 'eyebreak') {
    const c = eyes.centring();
    if (c > 0) return [base[0] + (0.5 - base[0]) * c, base[1] + (0.45 - base[1]) * c];
  }
  return base;
}

// State is pushed once a second, so it must NOT contain the hole's position - at 1 Hz
// the wander would move in visible steps. Instead it carries the drift *clock*, and
// each renderer evaluates the same deterministic wander function every frame. Every
// monitor therefore agrees on the position exactly, with no per-frame IPC.
function pushState() {
  const level = currentLevel();
  const virt = G.virtualBounds(screen.getAllDisplays());
  const shared = {
    mode: cfg.mode,
    level,
    motion: cfg.free.motion,
    pinned: cfg.free.center,
    driftBase: driftTime,
    driftEpoch: Date.now(),
    driftSpeed: cfg.hidden ? 0 : cfg.free.driftSpeed,
    virt,
    look: cfg.look,
    hud: cfg.hudVisible,
    hidden: cfg.hidden,
    idle: idleFactor(),
    idleSec: (() => { try { return powerMonitor.getSystemIdleTime(); } catch (e) { return -1; } })(),
    pomodoro: clock.status(),
    eyebreak: eyes.status(),
    monitors: overlays.size,
  };
  for (const e of overlays.values()) {
    if (!e.win || e.win.isDestroyed()) continue;
    e.win.webContents.send('state', Object.assign({}, shared, { bounds: e.bounds }));
  }
}

function persist() { if (!TEST_RUN) save(CONFIG_FILE, cfg); }

function setLevel(v) {
  // In eye-break mode this sets the RESTING size between breaks.
  if (cfg.mode === 'pomodoro') return;   // there the clock owns it
  cfg.free.level = Math.max(0, Math.min(1, v));
  pushState(); persist(); refreshTray();
}
function setMode(m) {
  cfg.mode = m;
  // Switching to pomodoro means "I want to focus now". Leaving the clock idle would
  // report level 0 and make the hole vanish, which reads as the app breaking rather
  // than as a timer waiting to be started.
  if (m === 'pomodoro') clock.start();
  else clock.pause();
  if (m === 'eyebreak') eyes.skip();      // start a fresh interval, not mid-countdown
  pushState(); persist(); refreshTray();
}
function setHidden(v) {
  cfg.hidden = v;
  pushState(); persist(); refreshTray();
}
function applyPreset(name) {
  const p = LOOKS.find(x => x.name === name);
  if (!p) return;
  cfg.preset = name;
  Object.assign(cfg.look, p.look);
  pushState(); persist(); refreshTray();
}

// ----------------------------------------------------------------- tray -----
function statusLine() {
  if (cfg.hidden) return 'Hidden';
  if (cfg.mode === 'free') return `Free · size ${Math.round(cfg.free.level * 100)}%`;
  if (cfg.mode === 'eyebreak') {
    const e = eyes.status();
    if (e.inBreak) return `LOOK AWAY · ${e.remaining}`;
    return `Eye break in ${e.remaining}${e.running ? '' : ' (paused)'}`;
  }
  const s = clock.status();
  if (s.phase === PHASE.IDLE) return 'Pomodoro · not started';
  return `${s.phase === PHASE.FOCUS ? 'Focus' : 'Break'} · ${s.remaining}${s.running ? '' : ' (paused)'}`;
}

const PIN_PRESETS = [
  ['Centre', 0.50, 0.40], ['Top left', 0.22, 0.22],
  ['Top right', 0.78, 0.22], ['Upper centre', 0.50, 0.20],
];
function nearPin(x, y) {
  return cfg.free.motion === 'still'
      && Math.abs(cfg.free.center[0] - x) < 0.005
      && Math.abs(cfg.free.center[1] - y) < 0.005;
}
function pinAt(x, y) {
  cfg.free.center = [x, y];
  cfg.free.motion = 'still';
  pushState(); persist(); refreshTray();
}
// The cursor is the only point the user can indicate, since the overlay is
// click-through. Converted into virtual-desktop uv so it works on any monitor.
function pinAtCursor() {
  const pt = screen.getCursorScreenPoint();
  const v = G.virtualBounds(screen.getAllDisplays());
  pinAt(Math.max(0, Math.min(1, (pt.x - v.x) / v.width)),
        Math.max(0, Math.min(1, (pt.y - v.y) / v.height)));
}

function refreshTray() {
  if (!tray) return;
  const s = clock.status();
  const es = eyes.status();
  const free = cfg.mode === 'free';
  const eye = cfg.mode === 'eyebreak';
  const wander = cfg.free.motion !== 'still';
  tray.setImage(nativeImage.createFromBuffer(trayIcon(32, cfg.hidden ? 0.3 : ((free || s.running) ? 1 : 0.45))));
  tray.setToolTip('Black Hole Pomodoro — ' + statusLine());

  const sizable = cfg.mode !== 'pomodoro' && !cfg.hidden;
  const sizeItem = (label, v) => ({
    label, type: 'radio', enabled: sizable,
    checked: sizable && Math.abs(cfg.free.level - v) < 0.001, click: () => setLevel(v),
  });
  const driftItem = (label, v) => ({
    label, type: 'radio', checked: Math.abs(cfg.free.driftSpeed - v) < 0.001,
    click: () => { cfg.free.driftSpeed = v; pushState(); persist(); refreshTray(); },
  });
  const pinItem = (label, x, y) => ({
    label, type: 'radio', checked: nearPin(x, y), click: () => pinAt(x, y),
  });
  const shrinkItem = (label, v) => ({
    label, type: 'radio', checked: Math.abs(cfg.eyebreak.recedeSec - v) < 0.01,
    click: () => { cfg.eyebreak.recedeSec = v; eyes.set(cfg.eyebreak); pushState(); persist(); refreshTray(); },
  });
  const curveItem = (label, v) => ({
    label, type: 'radio', checked: Math.abs(cfg.pomodoro.growthCurve - v) < 0.001,
    click: () => { cfg.pomodoro.growthCurve = v; clock.set(cfg.pomodoro); pushState(); persist(); refreshTray(); },
  });

  tray.setContextMenu(Menu.buildFromTemplate([
    { label: statusLine() + (overlays.size > 1 ? `  ·  ${overlays.size} monitors` : ''), enabled: false },
    { type: 'separator' },
    { label: 'Hide everything', type: 'checkbox', checked: cfg.hidden,
      click: () => setHidden(!cfg.hidden) },
    { type: 'separator' },
    { label: 'Free mode',     type: 'radio', checked: cfg.mode === 'free',
      enabled: !cfg.hidden, click: () => setMode('free') },
    { label: 'Pomodoro mode', type: 'radio', checked: cfg.mode === 'pomodoro',
      enabled: !cfg.hidden, click: () => setMode('pomodoro') },
    { label: 'Eye breaks (20-20-20)', type: 'radio', checked: eye,
      enabled: !cfg.hidden, click: () => setMode('eyebreak') },
    { type: 'separator' },
    ...(eye ? [
      { label: es.running ? 'Pause eye breaks' : 'Resume eye breaks', enabled: !cfg.hidden,
        click: () => { eyes.toggle(); pushState(); refreshTray(); } },
      { label: es.inBreak ? 'End this break now' : 'Take a break now', enabled: !cfg.hidden,
        click: () => { es.inBreak ? eyes.skip() : eyes.breakNow(); pushState(); refreshTray(); } },
      { label: `Breaks taken: ${es.completed}`, enabled: false },
    ] : [
    { label: s.running ? 'Pause' : (s.phase === PHASE.IDLE ? 'Start focus' : 'Resume'),
      enabled: !free && !cfg.hidden, click: () => { clock.toggle(); pushState(); refreshTray(); } },
    { label: 'Skip to ' + (s.phase === PHASE.FOCUS ? 'break' : 'focus'),
      enabled: !free && !cfg.hidden && s.phase !== PHASE.IDLE,
      click: () => { clock.skip(); pushState(); refreshTray(); } },
    { label: 'Reset', enabled: !free && !cfg.hidden && s.phase !== PHASE.IDLE,
      click: () => { clock.reset(); pushState(); refreshTray(); } },
    { label: `Completed: ${s.completed}`, enabled: false },
    ]),
    { type: 'separator' },
    { label: eye ? 'Size between breaks' : 'Size', submenu: [
      sizeItem('Hidden', 0), sizeItem('Small', 0.15), sizeItem('Medium', 0.4),
      sizeItem('Large', 0.7), sizeItem('Full', 1),
      ...(eye ? [{ type: 'separator' },
                 { label: 'What it shrinks back down to', enabled: false }] : []),
    ]},
    ...(eye ? [{ label: 'Shrink back', submenu: [
      shrinkItem('Quick (2s)', 2), shrinkItem('Gradual (6s)', 6),
      shrinkItem('Slow (12s)', 12), shrinkItem('Very slow (20s)', 20),
    ]}] : []),
    { label: 'Movement', submenu: [
      { label: overlays.size > 1 ? 'Wander across all monitors' : 'Wander across the screen',
        type: 'radio', checked: wander,
        click: () => { cfg.free.motion = 'wander'; pushState(); persist(); refreshTray(); } },
      { type: 'separator' },
      { label: 'Or keep it still at:', enabled: false },
      pinItem('Centre', 0.50, 0.40), pinItem('Top left', 0.22, 0.22),
      pinItem('Top right', 0.78, 0.22), pinItem('Upper centre', 0.50, 0.20),
      { label: 'At the cursor', type: 'radio',
        checked: !wander && !PIN_PRESETS.some(p => nearPin(p[1], p[2])), click: pinAtCursor },
    ]},
    { label: 'Drift speed', enabled: wander, submenu: [
      driftItem('Frozen', 0), driftItem('Slow', 0.35), driftItem('Normal', 1), driftItem('Fast', 2.5),
    ]},
    { label: 'Growth', submenu: [
      curveItem('Steady', 1), curveItem('Late surge', 2.5), curveItem('Dramatic', 5),
      { type: 'separator' },
      { label: 'How the hole grows through a focus block', enabled: false },
    ]},
    { label: 'Look', submenu: LOOKS.map(p => ({
      label: p.name, type: 'radio', checked: cfg.preset === p.name, click: () => applyPreset(p.name),
    }))},
    { type: 'separator' },
    { label: 'Fade when I am away', type: 'checkbox', checked: cfg.idle.enabled,
      click: () => { cfg.idle.enabled = !cfg.idle.enabled; pushState(); persist(); refreshTray(); } },
    { label: 'Show HUD', type: 'checkbox', checked: cfg.hudVisible,
      click: () => { cfg.hudVisible = !cfg.hudVisible; pushState(); persist(); refreshTray(); } },
    { label: 'Start with Windows', type: 'checkbox', checked: cfg.autostart,
      click: () => {
        cfg.autostart = !cfg.autostart;
        app.setLoginItemSettings({ openAtLogin: cfg.autostart });
        persist(); refreshTray();
      } },
    { type: 'separator' },
    { label: 'Quit', click: () => app.quit() },
  ]));
}

// ---------------------------------------------------------------- start -----
app.whenReady().then(() => {
  if (process.env.SMOKE) { try { require('fs').writeFileSync('smoke.txt', ''); } catch (e) {} }

  CONFIG_FILE = path.join(app.getPath('userData'), 'config.json');
  let shaderDefaults = {};
  try { shaderDefaults = build().defaults; LOOKS = presets(); }
  catch (e) { console.error('shader patch failed: ' + e.message); app.exit(1); return; }

  cfg = load(CONFIG_FILE, shaderDefaults);
  if (process.env.FAST) {
    cfg.pomodoro = Object.assign({}, cfg.pomodoro, { workMin: 0.5, breakMin: 10 / 60, collapseMin: 0.1 });
    cfg.mode = 'pomodoro'; cfg.hudVisible = true; cfg.hidden = false;
    cfg.eyebreak = Object.assign({}, cfg.eyebreak, { intervalMin: 10/60, breakSec: 6 });
  }
  if (process.env.IDLE_AFTER) {
    cfg.idle = Object.assign({}, cfg.idle,
      { enabled: true, afterSec: parseFloat(process.env.IDLE_AFTER), fadeSec: 5 });
  }
  clock = new Pomodoro(cfg.pomodoro);
  eyes = new EyeBreak(cfg.eyebreak);
  if (process.env.FAST) clock.start();

  session.defaultSession.setDisplayMediaRequestHandler((req, callback) => {
    desktopCapturer.getSources({ types: ['screen'] })
      .then(sources => {
        // Each overlay asks for the screen it is sitting on. Chromium reports a
        // display_id per source; fall back to index order if it is missing.
        const want = req.frame && req.frame.displayId;
        const match = sources.find(s => String(s.display_id) === String(want));
        callback({ video: match || sources[0] });
      })
      .catch(() => callback({}));
  }, { useSystemPicker: false });

  buildOverlays();
  screen.on('display-added', scheduleRebuild);
  screen.on('display-removed', scheduleRebuild);
  screen.on('display-metrics-changed', scheduleRebuild);

  tray = new Tray(nativeImage.createFromBuffer(trayIcon(32, 1)));
  refreshTray();

  // The overlay is click-through AND invisible to capture, so it cannot be pointed at.
  // The tray and these shortcuts are the whole control surface, which makes a working
  // quit key a safety requirement rather than a convenience.
  const nudge = d => setLevel(cfg.free.level + d);
  const keys = {
    'Control+Alt+Q': () => app.quit(),
    'Control+Alt+X': () => setHidden(!cfg.hidden),
    'Control+Alt+H': () => { cfg.hudVisible = !cfg.hudVisible; pushState(); persist(); refreshTray(); },
    'Control+Alt+Up': () => nudge(+0.05),
    'Control+Alt+Down': () => nudge(-0.05),
    'Control+Alt+]': () => nudge(+0.05),
    'Control+Alt+[': () => nudge(-0.05),
    'Control+Alt+0': () => setLevel(0),
    'Control+Alt+1': () => setLevel(1),
    'Control+Alt+P': () => setMode({ free: 'pomodoro', pomodoro: 'eyebreak', eyebreak: 'free' }[cfg.mode]),
    'Control+Alt+B': () => { if (cfg.mode === 'eyebreak') { eyes.breakNow(); pushState(); refreshTray(); } },
    'Control+Alt+S': () => { if (cfg.mode === 'pomodoro') { clock.toggle(); pushState(); refreshTray(); } },
    // Toggle: still -> wander again; wandering -> pin it where the cursor is.
    'Control+Alt+K': () => {
      if (cfg.free.motion === 'still') { cfg.free.motion = 'wander'; pushState(); persist(); refreshTray(); }
      else pinAtCursor();
    },
  };
  const failed = [];
  for (const [k, fn] of Object.entries(keys)) if (!globalShortcut.register(k, fn)) failed.push(k);
  if (process.env.SMOKE) {
    try {
      require('fs').appendFileSync('smoke.txt',
        'shortcuts: ' + (failed.length ? 'FAILED -> ' + failed.join(', ') : 'all registered ok') + '\n' +
        'monitors: ' + overlays.size + '\n');
    } catch (e) {}
  }
  if (failed.includes('Control+Alt+Q')) {
    console.error('FATAL: could not register the quit shortcut; refusing to run.');
    app.quit(); return;
  }

  lastTick = Date.now();
  setInterval(() => {
    const now = Date.now(), dt = (now - lastTick) / 1000; lastTick = now;
    if (!cfg.hidden) driftTime += dt * cfg.free.driftSpeed;
    if (cfg.mode === 'pomodoro') {
      const before = clock.phase + clock.running;
      if (!(cfg.pomodoro.pauseWhenIdle && idleFactor() <= 0)) clock.tick(dt);
      if (clock.phase + clock.running !== before) refreshTray();
    } else if (cfg.mode === 'eyebreak') {
      const before = eyes.phase;
      // Away from the desk already? Do not burn down the interval - otherwise you sit
      // back down and are immediately told to look away again.
      if (!(cfg.eyebreak.pauseWhenIdle && idleFactor() <= 0)) eyes.tick(dt);
      if (eyes.phase !== before) refreshTray();
    }
    pushState();
    tray.setToolTip('Black Hole Pomodoro — ' + statusLine());
  }, 1000);

  // Test hook: flip hidden on and off mid-run so the capture stop/restart cycle can be
  // exercised without a human clicking the tray.
  if (process.env.TOGGLE_HIDE) {
    const at = parseFloat(process.env.TOGGLE_HIDE) * 1000;
    setTimeout(() => { setHidden(true);  }, at);
    setTimeout(() => { setHidden(false); }, at * 2);
  }

  if (process.env.SMOKE) setTimeout(() => app.quit(), parseFloat(process.env.SMOKE) * 1000);
  ipcMain.on('renderer-fatal', (_e, msg) => { console.error('renderer fatal: ' + msg); app.quit(); });
});

app.on('will-quit', () => { globalShortcut.unregisterAll(); if (cfg) persist(); });
app.on('window-all-closed', () => app.quit());
