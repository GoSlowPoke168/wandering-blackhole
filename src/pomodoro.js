'use strict';
// The pomodoro clock. Pure state + time in, level out - no Electron, no GL, so it
// can be reasoned about and tested on its own.
//
// This is the piece upstream could not have: a Ghostty shader is stateless, so its
// README settles for "an hourly bell, not a per-streak stopwatch", anchoring growth
// to the wall clock because it cannot remember when your streak began. A host
// process can, so focus starts when you start it.

const PHASE = { IDLE: 'idle', FOCUS: 'focus', BREAK: 'break' };

class Pomodoro {
  constructor(opts = {}) {
    this.set(opts);
    this.phase = PHASE.IDLE;
    this.elapsed = 0;        // seconds spent in the current phase
    this.running = false;
    this.completed = 0;      // focus streaks finished
  }

  set(opts) {
    this.workMin     = opts.workMin     != null ? opts.workMin     : 55;
    this.breakMin    = opts.breakMin    != null ? opts.breakMin    : 5;
    this.collapseMin = opts.collapseMin != null ? opts.collapseMin : 1;
    // Growth exponent: 1 = steady, >1 holds the hole small and then surges near the
    // end. Applied here rather than via the shader's TOKEN_EASE so it shapes only the
    // pomodoro, leaving free-mode sizing linear and predictable.
    this.growthCurve = opts.growthCurve != null ? opts.growthCurve : 1;
  }

  start()  { if (this.phase === PHASE.IDLE) { this.phase = PHASE.FOCUS; this.elapsed = 0; } this.running = true; }
  pause()  { this.running = false; }
  resume() { if (this.phase !== PHASE.IDLE) this.running = true; }
  toggle() { this.running ? this.pause() : this.start(); }
  reset()  { this.phase = PHASE.IDLE; this.elapsed = 0; this.running = false; }

  // Jump straight to the other phase, keeping the clock running.
  skip() {
    if (this.phase === PHASE.FOCUS) { this.completed++; this.phase = PHASE.BREAK; }
    else { this.phase = PHASE.FOCUS; }
    this.elapsed = 0;
    this.running = true;
  }

  tick(dtSec) {
    if (!this.running || this.phase === PHASE.IDLE) return;
    this.elapsed += dtSec;
    const limit = this.phaseLengthSec();
    if (this.elapsed >= limit) {
      this.elapsed -= limit;
      if (this.phase === PHASE.FOCUS) { this.completed++; this.phase = PHASE.BREAK; }
      else { this.phase = PHASE.FOCUS; }
    }
  }

  phaseLengthSec() {
    return (this.phase === PHASE.BREAK ? this.breakMin : this.workMin) * 60;
  }

  remainingSec() {
    if (this.phase === PHASE.IDLE) return 0;
    return Math.max(0, this.phaseLengthSec() - this.elapsed);
  }

  // Hole size for the current moment, 0..1.
  //   focus: grows to full over the work period, then collapses over the final
  //          collapseMin - the collapse IS the "take your break now" signal.
  //   break: gone entirely, so the break feels like a break.
  level() {
    if (this.phase !== PHASE.FOCUS) return 0;
    const total = this.workMin * 60;
    const collapse = Math.min(this.collapseMin * 60, total * 0.5);
    const grow = total - collapse;
    if (this.elapsed < grow) {
      const t = Math.max(0, Math.min(1, this.elapsed / grow));
      return Math.pow(t, this.growthCurve);
    }
    // The collapse is the "take your break" signal, so keep it linear and abrupt
    // whatever the growth curve did - it should read as a drop, not a fade.
    return Math.max(0, 1 - (this.elapsed - grow) / collapse);
  }

  status() {
    const r = Math.round(this.remainingSec());
    const mm = String(Math.floor(r / 60)).padStart(2, '0');
    const ss = String(r % 60).padStart(2, '0');
    return {
      phase: this.phase,
      running: this.running,
      level: this.level(),
      remaining: `${mm}:${ss}`,
      remainingSec: r,
      completed: this.completed,
    };
  }
}

module.exports = { Pomodoro, PHASE };
