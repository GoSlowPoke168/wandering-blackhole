'use strict';
// Eye-break clock: the 20-20-20 rule. Every 20 minutes, look at something 20 feet away
// for 20 seconds.
//
// Unlike the pomodoro - where the break is the hole *vanishing* so you can rest - an eye
// break is the hole *swallowing the screen*, because the point is to make your work
// unreadable so you actually look away.
//
// Pure state + time in, level out. No Electron, no GL.

const PHASE = {
  WAITING: 'waiting',   // counting down; the hole sits at its resting size
  SWELL:   'swell',     // growing fast to engulf the screen
  HOLD:    'hold',      // full screen; look away
  RECEDE:  'recede',    // shrinking back to the resting size
};

class EyeBreak {
  constructor(opts = {}) {
    this.set(opts);
    this.phase = PHASE.WAITING;
    this.elapsed = 0;
    this.running = true;
    this.completed = 0;
  }

  set(opts) {
    this.intervalMin = opts.intervalMin != null ? opts.intervalMin : 20;
    this.breakSec    = opts.breakSec    != null ? opts.breakSec    : 20;
    this.swellSec    = opts.swellSec    != null ? opts.swellSec    : 1.5;
    this.recedeSec   = opts.recedeSec   != null ? opts.recedeSec   : 6;
  }

  phaseLengthSec() {
    switch (this.phase) {
      case PHASE.SWELL:  return this.swellSec;
      case PHASE.HOLD:   return this.breakSec;
      case PHASE.RECEDE: return this.recedeSec;
      default:           return this.intervalMin * 60;
    }
  }

  tick(dtSec) {
    if (!this.running) return;
    this.elapsed += dtSec;
    let guard = 0;
    while (this.elapsed >= this.phaseLengthSec() && guard++ < 8) {
      this.elapsed -= this.phaseLengthSec();
      switch (this.phase) {
        case PHASE.WAITING: this.phase = PHASE.SWELL;  break;
        case PHASE.SWELL:   this.phase = PHASE.HOLD;   break;
        case PHASE.HOLD:    this.phase = PHASE.RECEDE; break;
        case PHASE.RECEDE:  this.phase = PHASE.WAITING; this.completed++; break;
      }
    }
  }

  breakNow() { this.phase = PHASE.SWELL;   this.elapsed = 0; this.running = true; }
  skip()     { this.phase = PHASE.WAITING; this.elapsed = 0; this.running = true; }
  reset()    { this.skip(); this.completed = 0; }
  pause()    { this.running = false; }
  resume()   { this.running = true; }
  toggle()   { this.running = !this.running; }

  inBreak() { return this.phase !== PHASE.WAITING; }

  // Size override for the current moment, or null while waiting - in which case the
  // host uses whatever resting size the user picked.
  level(resting) {
    const r = Math.max(0, Math.min(1, resting == null ? 0 : resting));
    const f = Math.max(0, Math.min(1, this.elapsed / Math.max(0.001, this.phaseLengthSec())));
    switch (this.phase) {
      // Ease out, so it lunges early and settles - reads as being swallowed rather
      // than as a slider being dragged.
      case PHASE.SWELL:  return r + (1 - r) * (1 - Math.pow(1 - f, 3));
      case PHASE.HOLD:   return 1;
      // Smoothstep rather than a decaying power: it holds near full for a moment,
      // eases down through the middle and settles gently, which reads as deflating.
      // A power curve drops ~45% in the first quarter and snaps instead.
      case PHASE.RECEDE: return r + (1 - r) * (1 - f * f * (3 - 2 * f));
      default:           return null;
    }
  }

  // 0 while waiting, ramping to 1 across the break - used to slide the hole to the
  // middle of the screen as it swells, so it engulfs evenly.
  centring() {
    if (this.phase === PHASE.WAITING) return 0;
    const f = Math.max(0, Math.min(1, this.elapsed / Math.max(0.001, this.phaseLengthSec())));
    if (this.phase === PHASE.SWELL)  return 1 - Math.pow(1 - f, 3);
    // Same smoothstep the size uses, so it drifts back off-centre at exactly the rate
    // it shrinks. A faster curve here made it slide away while still large.
    if (this.phase === PHASE.RECEDE) return 1 - f * f * (3 - 2 * f);
    return 1;
  }

  status() {
    const rem = Math.max(0, this.phaseLengthSec() - this.elapsed);
    const mm = String(Math.floor(rem / 60)).padStart(2, '0');
    const ss = String(Math.floor(rem % 60)).padStart(2, '0');
    return {
      phase: this.phase,
      running: this.running,
      inBreak: this.inBreak(),
      remaining: this.phase === PHASE.WAITING ? `${mm}:${ss}` : `${Math.ceil(rem)}s`,
      remainingSec: rem,
      completed: this.completed,
    };
  }
}

module.exports = { EyeBreak, PHASE };
