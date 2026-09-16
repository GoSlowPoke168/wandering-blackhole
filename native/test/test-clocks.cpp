// Unit check for the pure clocks: no Windows, no GPU. Build/run: native\test\run.bat
#include "../src/eyebreak.h"
#include "../src/pomodoro.h"
#include <cstdio>

int fails = 0;
static void check(bool ok, const char* what) { if (!ok) { printf("  FAIL %s\n", what); fails++; } }
static void nearly(double got, double want, const char* what, double eps = 0.02) {
  if (std::fabs(got - want) > eps) { printf("  FAIL %s: got %.4f want %.4f\n", what, got, want); fails++; }
}

int main() {
  // Guard the defaults themselves: these were once built from a positional braced list, so
  // inserting a field shifted every later value along without a word from the compiler.
  { EyeBreakOpts d; PomodoroOpts p;
    check(d.intervalMin == 20 && d.breakSec == 20, "eye-break interval defaults");
    check(d.growthCurve > 1, "eye-break ramp is a curve, not a straight line");
    check(d.pauseWhenIdle, "eye breaks pause while away");
    check(p.workMin == 55 && p.breakMin == 5, "pomodoro defaults");
    check(!p.pauseWhenIdle, "the pomodoro clock keeps running while away"); }

  EyeBreakOpts o; o.intervalMin = 10 / 60.0; o.breakSec = 6; o.swellSec = 1.5; o.recedeSec = 6;
  EyeBreak e(o);
  const float peak = 1.0f;

  const double seed = EyeBreak::seedFor(peak);
  printf("eye-break waiting ramp (growthCurve %.1f), peak %.2f, seed %.3f:\n", o.growthCurve, peak, seed);
  for (int i = 0; i <= 10; i++) {
    EyeBreak t(o); t.elapsed = i * 1.0;                    // i seconds into a 10 s interval
    const double f = i / 10.0, want = seed + (peak - seed) * std::pow(f, o.growthCurve);
    printf("  %4.1f s  f=%.1f  level %.3f  (expected %.3f)\n", i * 1.0, f, t.level(peak), want);
    nearly(t.level(peak), want, "waiting ramp value");
  }
  // The ramp must be convex: barely more than the seed early, climbing hard late.
  { EyeBreak a(o), b(o); a.elapsed = 5; b.elapsed = 9;
    check(a.level(peak) < 0.3f * peak, "half way through the interval the hole is still small");
    check(b.level(peak) > 0.6f * peak, "near the break the hole is large"); }

  // Changing the size must change what is on screen at every point in the interval,
  // including the very start - otherwise the size keys look dead.
  { for (double at : { 0.0, 3.0, 7.0, 10.0 }) {
      EyeBreak a(o), b(o); a.elapsed = at; b.elapsed = at;
      check(b.level(0.45f) > a.level(0.40f) * 1.05f, "a size nudge moves the hole while waiting"); } }

  // The hole must stay visible the whole interval - vanishing reads as the app having died.
  { for (int i = 0; i <= 10; i++) { EyeBreak t(o); t.elapsed = i * 1.0;
      check(t.level(peak) > 0.01f, "the hole never disappears while waiting"); }
    EyeBreak fresh(o);
    check(fresh.level(peak) > 0.01f, "switching into eye-break mode shows something at once"); }

  // ...unless the chosen size is Hidden, which must still hide it.
  { EyeBreak a(o); check(a.level(0.f) == 0.f, "size Hidden stays hidden");
    EyeBreak b(o); b.phase = EyeBreak::RECEDE; b.elapsed = o.recedeSec;
    nearly(b.level(0.f), 0.0, "recede lands on nothing when the size is Hidden"); }

  // A small chosen size still leaves the ramp room to grow.
  { EyeBreak a(o), b(o); b.elapsed = o.intervalMin * 60;
    check(b.level(0.15f) > a.level(0.15f) * 2, "a small size still grows noticeably"); }

  // Phases must join continuously: ramp end == swell start, recede end == ramp start.
  { EyeBreak a(o); a.elapsed = o.intervalMin * 60;         // end of WAITING
    EyeBreak b(o); b.phase = EyeBreak::SWELL; b.elapsed = 0;
    nearly(a.level(peak), b.level(peak), "ramp end meets swell start"); }
  { EyeBreak a(o); a.phase = EyeBreak::RECEDE; a.elapsed = o.recedeSec;
    EyeBreak b(o); b.elapsed = 0;
    nearly(a.level(peak), b.level(peak), "recede end meets ramp start");
    nearly(a.level(peak), seed, "recede lands on the seed"); }
  { EyeBreak a(o); a.phase = EyeBreak::SWELL; a.elapsed = o.swellSec;
    nearly(a.level(peak), 1.0, "swell ends at full"); }

  // A smaller "size before a break" scales the ramp, not the break itself.
  { EyeBreak a(o); a.elapsed = o.intervalMin * 60;
    nearly(a.level(0.4f), 0.4, "ramp tops out at the chosen size");
    EyeBreak b(o); b.phase = EyeBreak::HOLD;
    nearly(b.level(0.4f), 1.0, "the break still engulfs the screen"); }

  printf(fails ? "\n%d FAILED\n" : "\nall clock checks pass\n", fails);
  return fails ? 1 : 0;
}
