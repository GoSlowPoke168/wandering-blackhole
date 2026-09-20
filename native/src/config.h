#pragma once
// Settings live at %APPDATA%\wandering-blackhole\config.json.
#include <string>
#include "renderer.h"
#include "pomodoro.h"
#include "eyebreak.h"

enum class Mode { Free, Pomodoro, EyeBreak };

struct Config {
  Mode mode = Mode::Free;
  bool hidden = false;
  bool paused = false;       // freeze the drift, the disk and the clocks; hiding implies it
  struct Free { float level = 0.25f; bool still = false; float center[2] = { 0.5f, 0.35f }; float driftSpeed = 1.f; } free;
  // Set in the constructor, never positionally: a braced list here silently shifts every
  // value along the moment a field is inserted into either Opts struct.
  PomodoroOpts pomodoro;
  EyeBreakOpts eyebreak;
  struct Idle { bool enabled = true; double afterSec = 90, fadeSec = 20; } idle;
  std::wstring preset = L"inferno";
  float look[LOOK_COUNT];
  bool autostart = false;
  bool hudVisible = false;
  float hudOpacity = 0.95f;   // how far the HUD's ground hides what is behind it

  Config();
};

std::wstring configPath();
Config loadConfig(const std::wstring& file);
bool saveConfig(const std::wstring& file, const Config& cfg);
