#pragma once
// Port of src/pomodoro.js. Pure state + time in, level out.
#include <algorithm>
#include <cmath>
#include <string>

struct PomodoroOpts { double workMin = 55, breakMin = 5, collapseMin = 1, growthCurve = 1; bool pauseWhenIdle = false; };

class Pomodoro {
public:
  enum Phase { IDLE, FOCUS, BREAK };
  Phase phase = IDLE;
  double elapsed = 0;
  bool running = false;
  int completed = 0;

  explicit Pomodoro(const PomodoroOpts& o = {}) { set(o); }
  void set(const PomodoroOpts& o) { opts_ = o; }

  void start()  { if (phase == IDLE) { phase = FOCUS; elapsed = 0; } running = true; }
  void pause()  { running = false; }
  void resume() { if (phase != IDLE) running = true; }
  void toggle() { running ? pause() : start(); }
  void reset()  { phase = IDLE; elapsed = 0; running = false; }
  void skip() {
    if (phase == FOCUS) { completed++; phase = BREAK; } else phase = FOCUS;
    elapsed = 0; running = true;
  }
  void tick(double dt) {
    if (!running || phase == IDLE) return;
    elapsed += dt;
    const double limit = phaseLengthSec();
    if (elapsed >= limit) {
      elapsed -= limit;
      if (phase == FOCUS) { completed++; phase = BREAK; } else phase = FOCUS;
    }
  }
  double phaseLengthSec() const { return (phase == BREAK ? opts_.breakMin : opts_.workMin) * 60; }
  double remainingSec() const { return phase == IDLE ? 0 : std::max(0.0, phaseLengthSec() - elapsed); }

  // Hole size for the current moment, 0..1: grows through focus, collapses over the
  // final collapseMin (the "take your break" signal), gone during the break.
  float level() const {
    if (phase != FOCUS) return 0;
    const double total = opts_.workMin * 60;
    const double collapse = std::min(opts_.collapseMin * 60, total * 0.5);
    const double grow = total - collapse;
    if (elapsed < grow) {
      const double t = std::max(0.0, std::min(1.0, elapsed / grow));
      return (float)std::pow(t, opts_.growthCurve);
    }
    return (float)std::max(0.0, 1 - (elapsed - grow) / collapse);
  }
  std::wstring remaining() const {
    const int r = (int)std::lround(remainingSec());
    wchar_t b[16]; swprintf(b, 16, L"%02d:%02d", r / 60, r % 60); return b;
  }
  const PomodoroOpts& opts() const { return opts_; }
private:
  PomodoroOpts opts_;
};
