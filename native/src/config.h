#pragma once
// Port of src/config.js. Same file, same schema, so settings carry over from the Electron
// build: %APPDATA%\blackhole-pomodoro\config.json.
#include <string>
#include "renderer.h"
#include "pomodoro.h"
#include "eyebreak.h"

enum class Mode { Free, Pomodoro, EyeBreak };

struct Config {
  Mode mode = Mode::Free;
  bool hidden = false;
  struct Free { float level = 0.25f; bool still = false; float center[2] = { 0.5f, 0.35f }; float driftSpeed = 1.f; } free;
  PomodoroOpts pomodoro{ 55, 5, 1, 2.5, false };
  EyeBreakOpts eyebreak{ 20, 20, 1.5, 6, true };
  struct Idle { bool enabled = true; double afterSec = 90, fadeSec = 20; } idle;
  std::wstring preset = L"inferno";
  float look[LOOK_COUNT];
  bool autostart = false;
  bool hudVisible = false;

  Config();
};

std::wstring configPath();
Config loadConfig(const std::wstring& file);
bool saveConfig(const std::wstring& file, const Config& cfg);
