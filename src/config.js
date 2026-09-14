'use strict';
// Persisted settings. Free-mode state (size, position, drift, look) survives
// restarts so the overlay comes back exactly as it was left.
const fs = require('fs');
const path = require('path');

function defaults(shaderDefaults) {
  return {
    mode: 'free',                 // 'free' | 'pomodoro' | 'eyebreak'
    hidden: false,                // keep running, draw nothing, stop capturing
    free: {
      level: 0.25,                // hole size 0..1
      motion: 'wander',           // 'wander' across all monitors, or 'still'
      center: [0.5, 0.35],        // virtual-desktop uv when still; y from the top
      driftSpeed: 1.0,            // multiplier on the drift clock; 0 = frozen
    },
    pomodoro: {
      workMin: 55,                // upstream's cycle
      breakMin: 5,
      collapseMin: 1,             // hole collapses over the final minute of focus
      growthCurve: 2.5,           // >1 keeps it small, then surges near the end
      pauseWhenIdle: false,       // also stop the clock while away, not just the visuals
    },
    eyebreak: {                   // the 20-20-20 rule
      intervalMin: 20,            // every 20 minutes...
      breakSec: 20,               // ...look away for 20 seconds
      swellSec: 1.5,              // time to engulf the screen
      recedeSec: 6,               // gradual shrink back afterwards
      pauseWhenIdle: true,        // away from the desk already: do not count down
    },
    idle: {
      enabled: true,
      afterSec: 90,               // inactivity before it starts fading (upstream's IDLE_FADE_SEC)
      fadeSec: 20,                // how long the fade itself takes
    },
    preset: 'inferno',
    look: Object.assign({}, shaderDefaults),
    autostart: false,
    hudVisible: false,
  };
}

// Merge saved values over defaults one level deep, keeping unknown keys out and
// never letting a truncated or hand-edited file break startup.
function merge(base, saved) {
  if (!saved || typeof saved !== 'object') return base;
  const out = Object.assign({}, base);
  for (const k of Object.keys(base)) {
    const b = base[k], s = saved[k];
    if (s === undefined) continue;
    if (Array.isArray(b)) out[k] = Array.isArray(s) && s.length === b.length ? s.slice() : b;
    else if (b && typeof b === 'object') out[k] = merge(b, s);
    else if (typeof s === typeof b) out[k] = s;
  }
  return out;
}

function load(file, shaderDefaults) {
  const base = defaults(shaderDefaults);
  let saved;
  try { saved = JSON.parse(fs.readFileSync(file, 'utf8')); }
  catch (e) { return base; }   // missing or corrupt: defaults rather than fail to start

  const cfg = merge(base, saved);
  // Migration: `free.pin` (0/1) became `free.motion` ('wander'/'still') when the hole
  // gained a virtual-desktop position. A single display's uv is the same as the
  // virtual desktop's, so a saved centre still points at the same place.
  if (saved.free && saved.free.pin >= 0.5 && (!saved.free.motion)) cfg.free.motion = 'still';
  return cfg;
}

// Write via a temp file so a crash mid-write cannot leave a truncated config.
function save(file, cfg) {
  try {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    const tmp = file + '.tmp';
    fs.writeFileSync(tmp, JSON.stringify(cfg, null, 2));
    fs.renameSync(tmp, file);
    return true;
  } catch (e) {
    return false;
  }
}

module.exports = { load, save, defaults };
