#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "config.h"
#include "geometry.h"
#include "overlay.h"

// What the render thread needs from the host, refreshed every tick. The port of
// pushState()'s `shared` object: it carries the drift *clock*, not the position, so the
// wander is evaluated per frame and never steps.
struct Snapshot {
  bool hidden = false, hud = false;
  Mode mode = Mode::Free;
  float level = 0;                 // target size incl. idle fade
  bool still = false; UV pinned{ 0.5f, 0.35f };
  double driftBase = 0, driftSpeed = 1, driftEpoch = 0;   // seconds (QPC)
  float centring = 0;              // eye break: slide toward the middle
  RECT virt{ 0, 0, 1, 1 };
  float look[LOOK_COUNT]{};
  std::wstring hudText;
  bool restartCapture = false;     // resume / unlock / test hook; consumed by the render thread
};

class App {
public:
  int run(HINSTANCE hinst);
private:
  // ---- UI thread ----
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT handle(HWND, UINT, WPARAM, LPARAM);
  bool init();
  bool gpuGuard();
  void buildOverlays();
  void tick();
  float idleFactor();
  int idleSeconds();
  float currentLevel();
  std::wstring statusLine();
  void pushState();
  void persist();
  void setLevel(float v);
  void setMode(Mode m);
  void setHidden(bool v);
  void applyPreset(const std::wstring& name);
  void pinAt(float x, float y);
  void pinAtCursor();
  void setAutostart(bool on);
  void refreshTray();
  void showMenu();
  void onCommand(int id);
  void onHotkey(int id);
  void smoke(const std::wstring& line);
  void quit();
  // ---- render thread ----
  void renderLoop();
  void renderStats(double t, double& tStat, int& frames, int& newFrames, int& echoes,
                   int& iters, int& skipped, double& cpuMs, double& waitMs, double& acqMs,
                   float dirtyPct, float shownLevel, UV shownCenter, bool captureLive);

  HINSTANCE hinst_ = nullptr;
  HWND hwnd_ = nullptr;
  HICON icon_ = nullptr;
  bool trayAdded_ = false;
  std::wstring configFile_, hlsl_, lastTip_;
  Config cfg_;
  Pomodoro clock_;
  EyeBreak eyes_;
  double driftTime_ = 0, lastTick_ = 0, startTime_ = 0;
  bool testRun_ = false, smokeOn_ = false;
  int lastTrayIconState_ = -1;

  std::vector<std::unique_ptr<Overlay>> overlays_;
  std::mutex overlaysMu_;
  std::vector<RECT> rects_;      // overlay rects, UI thread only - so the tick never waits on the render loop
  Snapshot snap_;
  std::mutex snapMu_;
  std::wstring renderStats_;     // written by the render thread, read at tray refresh
  std::mutex statsMu_;
  std::thread render_;
  std::atomic<bool> running_{ false };
};
