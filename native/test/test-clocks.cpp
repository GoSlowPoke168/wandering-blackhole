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

  printf("eye-break waiting ramp (growthCurve %.1f), peak %.2f:\n", o.growthCurve, peak);
  for (int i = 0; i <= 10; i++) {
    EyeBreak t(o); t.elapsed = i * 1.0;                    // i seconds into a 10 s interval
    const double f = i / 10.0, want = std::pow(f, o.growthCurve);
    printf("  %4.1f s  f=%.1f  level %.3f  (expected %.3f)\n", i * 1.0, f, t.level(peak), want);
    nearly(t.level(peak), want, "waiting ramp value");
  }
  // The ramp must be convex: near nothing early, climbing hard late.
  { EyeBreak a(o), b(o); a.elapsed = 5; b.elapsed = 9;
    check(a.level(peak) < 0.2f, "half way through the interval the hole is still small");
    check(b.level(peak) > 0.6f, "near the break the hole is large"); }

  // Phases must join continuously: ramp end == swell start, recede end == ramp start.
  { EyeBreak a(o); a.elapsed = o.intervalMin * 60;         // end of WAITING
    EyeBreak b(o); b.phase = EyeBreak::SWELL; b.elapsed = 0;
    nearly(a.level(peak), b.level(peak), "ramp end meets swell start"); }
  { EyeBreak a(o); a.phase = EyeBreak::RECEDE; a.elapsed = o.recedeSec;
    EyeBreak b(o); b.elapsed = 0;
    nearly(a.level(peak), b.level(peak), "recede end meets ramp start"); }
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
