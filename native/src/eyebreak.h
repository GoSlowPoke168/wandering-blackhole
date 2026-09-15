#pragma once
// Port of src/eyebreak.js: the 20-20-20 rule. Every 20 minutes the hole swallows the
// screen for 20 seconds so you actually look away, then shrinks back.
#include <algorithm>
#include <cmath>
#include <string>

struct EyeBreakOpts { double intervalMin = 20, breakSec = 20, swellSec = 1.5, recedeSec = 6; bool pauseWhenIdle = true; };

class EyeBreak {
public:
  enum Phase { WAITING, SWELL, HOLD, RECEDE };
  Phase phase = WAITING;
  double elapsed = 0;
  bool running = true;
  int completed = 0;

  explicit EyeBreak(const EyeBreakOpts& o = {}) { set(o); }
  void set(const EyeBreakOpts& o) { opts_ = o; }

  double phaseLengthSec() const {
    switch (phase) {
      case SWELL:  return opts_.swellSec;
      case HOLD:   return opts_.breakSec;
      case RECEDE: return opts_.recedeSec;
      default:     return opts_.intervalMin * 60;
    }
  }
  void tick(double dt) {
    if (!running) return;
    elapsed += dt;
    int guard = 0;
    while (elapsed >= phaseLengthSec() && guard++ < 8) {
      elapsed -= phaseLengthSec();
      switch (phase) {
        case WAITING: phase = SWELL;  break;
        case SWELL:   phase = HOLD;   break;
        case HOLD:    phase = RECEDE; break;
        case RECEDE:  phase = WAITING; completed++; break;
      }
    }
  }
  void breakNow() { phase = SWELL;   elapsed = 0; running = true; }
  void skip()     { phase = WAITING; elapsed = 0; running = true; }
  void reset()    { skip(); completed = 0; }
  void pause()    { running = false; }
  void resume()   { running = true; }
  void toggle()   { running = !running; }
  bool inBreak() const { return phase != WAITING; }

  // Size override for the moment, or a negative value while waiting (host uses the
  // resting size then).
  float level(float resting) const {
    const double r = std::max(0.f, std::min(1.f, resting));
    const double f = frac();
    switch (phase) {
      case SWELL:  return (float)(r + (1 - r) * (1 - std::pow(1 - f, 3)));
      case HOLD:   return 1;
      case RECEDE: return (float)(r + (1 - r) * (1 - f * f * (3 - 2 * f)));
      default:     return -1;
    }
  }
  // 0 while waiting, ramping to 1 across the break: slides the hole to the middle so it
  // engulfs evenly.
  float centring() const {
    if (phase == WAITING) return 0;
    const double f = frac();
    if (phase == SWELL)  return (float)(1 - std::pow(1 - f, 3));
    if (phase == RECEDE) return (float)(1 - f * f * (3 - 2 * f));
    return 1;
  }
  std::wstring remaining() const {
    const double rem = std::max(0.0, phaseLengthSec() - elapsed);
    wchar_t b[16];
    if (phase == WAITING) swprintf(b, 16, L"%02d:%02d", (int)(rem / 60), (int)std::fmod(rem, 60));
    else swprintf(b, 16, L"%ds", (int)std::ceil(rem));
    return b;
  }
  const EyeBreakOpts& opts() const { return opts_; }
private:
  double frac() const { return std::max(0.0, std::min(1.0, elapsed / std::max(0.001, phaseLengthSec()))); }
  EyeBreakOpts opts_;
};
