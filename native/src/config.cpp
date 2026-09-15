#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "config.h"
#include "presets.gen.h"
#include <windows.h>
#include <shlobj.h>
#include <winrt/base.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "shell32.lib")

using namespace winrt::Windows::Data::Json;

Config::Config() { for (int i = 0; i < LOOK_COUNT; i++) look[i] = kLookDefaults[i]; }

std::wstring configPath() {
  wchar_t* appdata = nullptr;
  std::wstring dir = L".";
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) { dir = appdata; CoTaskMemFree(appdata); }
  return dir + L"\\blackhole-pomodoro\\config.json";
}

// Typed lookups that fall back to the default on a missing or mistyped key, which is
// what config.js's merge did: a hand-edited or truncated file never breaks startup.
static double num(JsonObject const& o, const wchar_t* k, double d) {
  auto v = o.TryLookup(k); return v && v.ValueType() == JsonValueType::Number ? v.GetNumber() : d;
}
static bool boolv(JsonObject const& o, const wchar_t* k, bool d) {
  auto v = o.TryLookup(k); return v && v.ValueType() == JsonValueType::Boolean ? v.GetBoolean() : d;
}
static std::wstring str(JsonObject const& o, const wchar_t* k, const std::wstring& d) {
  auto v = o.TryLookup(k); return v && v.ValueType() == JsonValueType::String ? std::wstring(v.GetString()) : d;
}
static JsonObject obj(JsonObject const& o, const wchar_t* k) {
  auto v = o.TryLookup(k); return v && v.ValueType() == JsonValueType::Object ? v.GetObject() : nullptr;
}

static std::wstring utf8ToWide(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n); return w;
}
static std::string wideToUtf8(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr); return s;
}

Config loadConfig(const std::wstring& file) {
  Config c;
  std::ifstream f(file, std::ios::binary);
  if (!f) return c;
  std::stringstream ss; ss << f.rdbuf();
  JsonObject root{ nullptr };
  try { root = JsonObject::Parse(utf8ToWide(ss.str())); } catch (...) { return c; }

  const std::wstring mode = str(root, L"mode", L"free");
  c.mode = mode == L"pomodoro" ? Mode::Pomodoro : mode == L"eyebreak" ? Mode::EyeBreak : Mode::Free;
  c.hidden = boolv(root, L"hidden", c.hidden);
  if (auto o = obj(root, L"free")) {
    c.free.level = (float)num(o, L"level", c.free.level);
    c.free.still = str(o, L"motion", c.free.still ? L"still" : L"wander") == L"still";
    c.free.driftSpeed = (float)num(o, L"driftSpeed", c.free.driftSpeed);
    auto ctr = o.TryLookup(L"center");
    if (ctr && ctr.ValueType() == JsonValueType::Array) {
      auto a = ctr.GetArray();
      if (a.Size() == 2) for (unsigned i = 0; i < 2; i++) if (a.GetAt(i).ValueType() == JsonValueType::Number) c.free.center[i] = (float)a.GetNumberAt(i);
    }
  }
  if (auto o = obj(root, L"pomodoro")) {
    c.pomodoro.workMin = num(o, L"workMin", c.pomodoro.workMin);
    c.pomodoro.breakMin = num(o, L"breakMin", c.pomodoro.breakMin);
    c.pomodoro.collapseMin = num(o, L"collapseMin", c.pomodoro.collapseMin);
    c.pomodoro.growthCurve = num(o, L"growthCurve", c.pomodoro.growthCurve);
    c.pomodoro.pauseWhenIdle = boolv(o, L"pauseWhenIdle", c.pomodoro.pauseWhenIdle);
  }
  if (auto o = obj(root, L"eyebreak")) {
    c.eyebreak.intervalMin = num(o, L"intervalMin", c.eyebreak.intervalMin);
    c.eyebreak.breakSec = num(o, L"breakSec", c.eyebreak.breakSec);
    c.eyebreak.swellSec = num(o, L"swellSec", c.eyebreak.swellSec);
    c.eyebreak.recedeSec = num(o, L"recedeSec", c.eyebreak.recedeSec);
    c.eyebreak.pauseWhenIdle = boolv(o, L"pauseWhenIdle", c.eyebreak.pauseWhenIdle);
  }
  if (auto o = obj(root, L"idle")) {
    c.idle.enabled = boolv(o, L"enabled", c.idle.enabled);
    c.idle.afterSec = num(o, L"afterSec", c.idle.afterSec);
    c.idle.fadeSec = num(o, L"fadeSec", c.idle.fadeSec);
  }
  c.preset = str(root, L"preset", c.preset);
  if (auto o = obj(root, L"look"))
    for (int i = 0; i < LOOK_COUNT; i++) c.look[i] = (float)num(o, utf8ToWide(kLookNames[i]).c_str(), c.look[i]);
  c.autostart = boolv(root, L"autostart", c.autostart);
  c.hudVisible = boolv(root, L"hudVisible", c.hudVisible);
  return c;
}

static JsonValue N(double v) { return JsonValue::CreateNumberValue(v); }
static JsonValue B(bool v) { return JsonValue::CreateBooleanValue(v); }
static JsonValue S(const std::wstring& v) { return JsonValue::CreateStringValue(v); }

bool saveConfig(const std::wstring& file, const Config& c) {
  JsonObject root;
  root.Insert(L"mode", S(c.mode == Mode::Pomodoro ? L"pomodoro" : c.mode == Mode::EyeBreak ? L"eyebreak" : L"free"));
  root.Insert(L"hidden", B(c.hidden));
  JsonObject fr; fr.Insert(L"level", N(c.free.level)); fr.Insert(L"motion", S(c.free.still ? L"still" : L"wander"));
  JsonArray ctr; ctr.Append(N(c.free.center[0])); ctr.Append(N(c.free.center[1])); fr.Insert(L"center", ctr);
  fr.Insert(L"driftSpeed", N(c.free.driftSpeed)); root.Insert(L"free", fr);
  JsonObject po; po.Insert(L"workMin", N(c.pomodoro.workMin)); po.Insert(L"breakMin", N(c.pomodoro.breakMin));
  po.Insert(L"collapseMin", N(c.pomodoro.collapseMin)); po.Insert(L"growthCurve", N(c.pomodoro.growthCurve));
  po.Insert(L"pauseWhenIdle", B(c.pomodoro.pauseWhenIdle)); root.Insert(L"pomodoro", po);
  JsonObject ey; ey.Insert(L"intervalMin", N(c.eyebreak.intervalMin)); ey.Insert(L"breakSec", N(c.eyebreak.breakSec));
  ey.Insert(L"swellSec", N(c.eyebreak.swellSec)); ey.Insert(L"recedeSec", N(c.eyebreak.recedeSec));
  ey.Insert(L"pauseWhenIdle", B(c.eyebreak.pauseWhenIdle)); root.Insert(L"eyebreak", ey);
  JsonObject id; id.Insert(L"enabled", B(c.idle.enabled)); id.Insert(L"afterSec", N(c.idle.afterSec)); id.Insert(L"fadeSec", N(c.idle.fadeSec));
  root.Insert(L"idle", id);
  root.Insert(L"preset", S(c.preset));
  JsonObject lk; for (int i = 0; i < LOOK_COUNT; i++) lk.Insert(utf8ToWide(kLookNames[i]), N(c.look[i])); root.Insert(L"look", lk);
  root.Insert(L"autostart", B(c.autostart));
  root.Insert(L"hudVisible", B(c.hudVisible));

  // Temp file + rename, so a crash mid-write cannot leave a truncated config.
  const std::wstring dir = file.substr(0, file.find_last_of(L"\\/"));
  SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
  const std::wstring tmp = file + L".tmp";
  {
    std::ofstream out(tmp, std::ios::binary);
    if (!out) return false;
    out << wideToUtf8(std::wstring(root.Stringify()));
  }
  return MoveFileExW(tmp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
