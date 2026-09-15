#include "app.h"
#include "icon.h"
#include "presets.gen.h"
#include <dcomp.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "wtsapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

static const UINT WM_APP_TRAY = WM_APP + 1;
static const UINT_PTR TIMER_TICK = 1, TIMER_REBUILD = 2;
static const wchar_t* kHostClass = L"BlackHolePomodoroHost";
static UINT g_taskbarCreated = 0;

// Menu command ids. Ranges for the parametrised radio groups.
enum {
  ID_HIDE = 100, ID_MODE_FREE, ID_MODE_POMO, ID_MODE_EYE, ID_EYE_TOGGLE, ID_EYE_BREAKNOW,
  ID_POMO_TOGGLE, ID_POMO_SKIP, ID_POMO_RESET, ID_WANDER, ID_PIN_CURSOR, ID_IDLE_FADE, ID_HUD,
  ID_AUTOSTART, ID_QUIT,
  ID_SIZE = 200, ID_SHRINK = 210, ID_DRIFT = 220, ID_PIN = 230, ID_CURVE = 240, ID_PRESET = 300,
};
struct Named { const wchar_t* label; float v; };
static const Named kSizes[]  = { { L"Hidden", 0 }, { L"Small", 0.15f }, { L"Medium", 0.4f }, { L"Large", 0.7f }, { L"Full", 1 } };
static const Named kDrifts[] = { { L"Frozen", 0 }, { L"Slow", 0.35f }, { L"Normal", 1 }, { L"Fast", 2.5f } };
static const Named kShrink[] = { { L"Quick (2s)", 2 }, { L"Gradual (6s)", 6 }, { L"Slow (12s)", 12 }, { L"Very slow (20s)", 20 } };
static const Named kCurves[] = { { L"Steady", 1 }, { L"Late surge", 2.5f }, { L"Dramatic", 5 } };
struct Pin { const wchar_t* label; float x, y; };
static const Pin kPins[] = { { L"Centre", 0.50f, 0.40f }, { L"Top left", 0.22f, 0.22f }, { L"Top right", 0.78f, 0.22f }, { L"Upper centre", 0.50f, 0.20f } };

// Hotkeys: the overlay is click-through and invisible to capture, so these and the tray
// are the whole control surface. A working quit key is a safety requirement.
enum { HK_QUIT = 1, HK_HIDE, HK_HUD, HK_UP, HK_DOWN, HK_RBRACKET, HK_LBRACKET, HK_ZERO, HK_ONE, HK_MODE, HK_BREAK, HK_START, HK_PIN };
static const struct { int id; UINT vk; const wchar_t* name; } kHotkeys[] = {
  { HK_QUIT, 'Q', L"Control+Alt+Q" }, { HK_HIDE, 'X', L"Control+Alt+X" }, { HK_HUD, 'H', L"Control+Alt+H" },
  { HK_UP, VK_UP, L"Control+Alt+Up" }, { HK_DOWN, VK_DOWN, L"Control+Alt+Down" },
  { HK_RBRACKET, VK_OEM_6, L"Control+Alt+]" }, { HK_LBRACKET, VK_OEM_4, L"Control+Alt+[" },
  { HK_ZERO, '0', L"Control+Alt+0" }, { HK_ONE, '1', L"Control+Alt+1" }, { HK_MODE, 'P', L"Control+Alt+P" },
  { HK_BREAK, 'B', L"Control+Alt+B" }, { HK_START, 'S', L"Control+Alt+S" }, { HK_PIN, 'K', L"Control+Alt+K" },
};

static double now() {
  static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
  LARGE_INTEGER n; QueryPerformanceCounter(&n); return double(n.QuadPart) / f.QuadPart;
}
static std::wstring env(const wchar_t* name) {
  wchar_t b[256]; DWORD n = GetEnvironmentVariableW(name, b, 256); return n && n < 256 ? std::wstring(b, n) : L"";
}
static std::wstring exePath() { wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH); return p; }
static std::wstring exeDir() { const std::wstring p = exePath(); return p.substr(0, p.find_last_of(L"\\/")); }
static bool approx(float a, float b, float eps = 0.001f) { return std::fabs(a - b) < eps; }

// ------------------------------------------------------------------ start -----
int App::run(HINSTANCE hinst) {
  hinst_ = hinst;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  if (!init()) return 1;
  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
  return (int)msg.wParam;
}

bool App::init() {
  const std::wstring smokeSec = env(L"SMOKE"), fast = env(L"FAST"), idleAfter = env(L"IDLE_AFTER");
  testRun_ = !fast.empty() || !idleAfter.empty() || !smokeSec.empty();
  smokeOn_ = !smokeSec.empty();
  if (smokeOn_) { std::ofstream(L"smoke.txt", std::ios::trunc); }

  configFile_ = configPath();
  hlsl_ = exeDir() + L"\\shader\\blackhole.hlsl";
  cfg_ = loadConfig(configFile_);
  if (!fast.empty()) {
    cfg_.pomodoro.workMin = 0.5; cfg_.pomodoro.breakMin = 10 / 60.0; cfg_.pomodoro.collapseMin = 0.1;
    cfg_.mode = Mode::Pomodoro; cfg_.hudVisible = true; cfg_.hidden = false;
    cfg_.eyebreak.intervalMin = 10 / 60.0; cfg_.eyebreak.breakSec = 6;
  }
  if (!idleAfter.empty()) { cfg_.idle.enabled = true; cfg_.idle.afterSec = _wtof(idleAfter.c_str()); cfg_.idle.fadeSec = 5; }
  clock_ = Pomodoro(cfg_.pomodoro);
  eyes_ = EyeBreak(cfg_.eyebreak);
  if (!fast.empty()) clock_.start();

  WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = hinst_; wc.lpszClassName = kHostClass;
  RegisterClassW(&wc);
  // A real (never shown) top-level window, not a message-only one: WM_DISPLAYCHANGE and
  // WM_POWERBROADCAST are broadcast to top-level windows only.
  hwnd_ = CreateWindowExW(0, kHostClass, L"Black Hole Pomodoro", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, hinst_, this);
  if (!hwnd_) return false;
  WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

  std::wstring failed;
  for (const auto& k : kHotkeys)
    if (!RegisterHotKey(hwnd_, k.id, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, k.vk)) failed += std::wstring(failed.empty() ? L"" : L", ") + k.name;
  smoke(L"shortcuts: " + (failed.empty() ? L"all registered ok" : L"FAILED -> " + failed));
  if (failed.find(L"Control+Alt+Q") != std::wstring::npos) {
    MessageBoxW(nullptr, L"Could not register Ctrl+Alt+Q - is Black Hole Pomodoro already running?\n\nRefusing to run without a quit key.",
                L"Black Hole Pomodoro", MB_ICONERROR);
    return false;
  }

  if (!gpuGuard()) return false;
  buildOverlays();
  smoke(L"monitors: " + std::to_wstring(overlays_.size()));

  startTime_ = lastTick_ = now();
  pushState();
  refreshTray();
  running_ = true;
  render_ = std::thread(&App::renderLoop, this);
  SetTimer(hwnd_, TIMER_TICK, 50, nullptr);

  if (!env(L"TOGGLE_HIDE").empty()) {
    const UINT at = (UINT)(_wtof(env(L"TOGGLE_HIDE").c_str()) * 1000);
    SetTimer(hwnd_, 10, at, nullptr); SetTimer(hwnd_, 11, at * 2, nullptr);
  }
  if (!env(L"RESTART_AT").empty()) SetTimer(hwnd_, 12, (UINT)(_wtof(env(L"RESTART_AT").c_str()) * 1000), nullptr);
  if (smokeOn_) SetTimer(hwnd_, 13, (UINT)(_wtof(smokeSec.c_str()) * 1000), nullptr);
  return true;
}

// Hybrid laptops: NVIDIA's driver profiles match on exe name and can force this process
// onto the dGPU, under which DXGI remaps the panel's output and Desktop Duplication fails.
// If the output's adapter is not the one that really drives it (QueryDisplayConfig knows),
// pin this exe to the right GPU in Windows' per-app graphics settings and relaunch once.
bool App::gpuGuard() {
  std::map<std::wstring, LUID> owner;
  UINT32 nPath = 0, nMode = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nPath, &nMode) == ERROR_SUCCESS) {
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(nPath); std::vector<DISPLAYCONFIG_MODE_INFO> modes(nMode);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nPath, paths.data(), &nMode, modes.data(), nullptr) == ERROR_SUCCESS)
      for (UINT32 i = 0; i < nPath; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME sn{};
        sn.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(sn), paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id };
        if (DisplayConfigGetDeviceInfo(&sn.header) == ERROR_SUCCESS) owner[sn.viewGdiDeviceName] = paths[i].sourceInfo.adapterId;
      }
  }
  ComPtr<IDXGIFactory1> f; CreateDXGIFactory1(IID_PPV_ARGS(&f));
  bool remapped = false; LUID ownerLuid{};
  for (UINT ai = 0; ; ai++) {
    ComPtr<IDXGIAdapter1> a; if (f->EnumAdapters1(ai, &a) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 ad{}; a->GetDesc1(&ad);
    for (UINT oi = 0; ; oi++) {
      ComPtr<IDXGIOutput> o; if (a->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_OUTPUT_DESC od{}; o->GetDesc(&od);
      auto it = owner.find(od.DeviceName);
      if (it != owner.end() && (it->second.LowPart != ad.AdapterLuid.LowPart || it->second.HighPart != ad.AdapterLuid.HighPart)) {
        remapped = true; ownerLuid = it->second;
      }
    }
  }
  if (!remapped) return true;
  if (!env(L"BHP_RELAUNCHED").empty()) {
    MessageBoxW(nullptr, L"Windows keeps assigning this process to a GPU that does not drive the display, so the desktop cannot be captured.\n\n"
                L"Settings > System > Display > Graphics: add BlackHolePomodoro.exe and pick the GPU that drives your screen.",
                L"Black Hole Pomodoro", MB_ICONWARNING);
    return true;   // run anyway; the capture retry loop will keep trying
  }
  // Which preference names the owning adapter? 1 = power saving, 2 = high performance.
  int pref = 2;
  ComPtr<IDXGIFactory6> f6;
  if (SUCCEEDED(f.As(&f6))) {
    ComPtr<IDXGIAdapter1> low;
    if (SUCCEEDED(f6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_MINIMUM_POWER, IID_PPV_ARGS(&low)))) {
      DXGI_ADAPTER_DESC1 ld{}; low->GetDesc1(&ld);
      if (ld.AdapterLuid.LowPart == ownerLuid.LowPart && ld.AdapterLuid.HighPart == ownerLuid.HighPart) pref = 1;
    }
  }
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\DirectX\\UserGpuPreferences", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
    const std::wstring v = L"GpuPreference=" + std::to_wstring(pref) + L";";
    RegSetValueExW(key, exePath().c_str(), 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
  }
  SetEnvironmentVariableW(L"BHP_RELAUNCHED", L"1");
  STARTUPINFOW si{ sizeof(si) }; PROCESS_INFORMATION pi{};
  std::wstring cmd = GetCommandLineW();
  if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return false;
  }
  return true;
}

void App::buildOverlays() {
  std::lock_guard<std::mutex> lock(overlaysMu_);
  overlays_.clear();
  ComPtr<IDXGIFactory1> f; CreateDXGIFactory1(IID_PPV_ARGS(&f));
  for (UINT ai = 0; ; ai++) {
    ComPtr<IDXGIAdapter1> a; if (f->EnumAdapters1(ai, &a) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 ad{}; a->GetDesc1(&ad);
    if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    for (UINT oi = 0; ; oi++) {
      ComPtr<IDXGIOutput> o; if (a->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND) break;
      DXGI_OUTPUT_DESC od{}; o->GetDesc(&od);
      if (!od.AttachedToDesktop) continue;
      ComPtr<IDXGIOutput1> o1; if (FAILED(o.As(&o1))) continue;
      auto ov = std::make_unique<Overlay>(a, o1, hinst_);
      std::wstring err;
      if (!ov->init(hlsl_, &err)) {
        smoke(L"overlay " + std::wstring(od.DeviceName) + L" failed: " + err);
        if (err.rfind(L"shader:", 0) == 0) MessageBoxW(nullptr, err.c_str(), L"Black Hole Pomodoro - shader", MB_ICONERROR);
        continue;
      }
      ov->setDebug(smokeOn_);
      wchar_t line[256];
      swprintf(line, 256, L"setup: display %s window %ldx%ld at (%ld,%ld) scale %.2f capture-excluded %s",
               od.DeviceName, (long)ov->width(), (long)ov->height(), ov->rect().left, ov->rect().top, ov->scale(), ov->affinityOk() ? L"yes" : L"NO");
      smoke(line);
      overlays_.push_back(std::move(ov));
    }
  }
  rects_.clear();
  for (auto& o : overlays_) rects_.push_back(o->rect());
}

// ------------------------------------------------------------------ state -----
int App::idleSeconds() { LASTINPUTINFO li{ sizeof(li) }; return GetLastInputInfo(&li) ? (int)((GetTickCount() - li.dwTime) / 1000) : 0; }
float App::idleFactor() {
  if (!cfg_.idle.enabled) return 1;
  const double over = idleSeconds() - cfg_.idle.afterSec;
  if (over <= 0) return 1;
  return (float)std::max(0.0, 1 - over / std::max(1.0, cfg_.idle.fadeSec));
}
float App::currentLevel() {
  if (cfg_.hidden) return 0;
  float base;
  if (cfg_.mode == Mode::Pomodoro) base = clock_.level();
  else if (cfg_.mode == Mode::EyeBreak) { const float o = eyes_.level(cfg_.free.level); base = o < 0 ? cfg_.free.level : o; }
  else base = cfg_.free.level;
  // An eye break must not be faded out by the idle timer.
  const float idleSafe = (cfg_.mode == Mode::EyeBreak && eyes_.inBreak()) ? 1 : idleFactor();
  return base * idleSafe;
}

void App::pushState() {
  const float level = currentLevel();
  const std::vector<RECT>& rects = rects_;
  wchar_t hud[512];
  const float idle = idleFactor();
  const wchar_t* modeName = cfg_.hidden ? L"hidden" : cfg_.mode == Mode::Pomodoro ? L"pomodoro" : cfg_.mode == Mode::EyeBreak ? L"eyebreak" : L"free";
  std::wstring phase;
  if (cfg_.mode == Mode::Pomodoro && !cfg_.hidden) {
    const wchar_t* p = clock_.phase == Pomodoro::FOCUS ? L"focus" : clock_.phase == Pomodoro::BREAK ? L"break" : L"idle";
    phase = std::wstring(L"  ") + p + L" " + clock_.remaining() + (clock_.running ? L"" : L" (paused)");
  } else if (cfg_.mode == Mode::EyeBreak && !cfg_.hidden) {
    phase = eyes_.inBreak() ? L"  LOOK AWAY  " + eyes_.remaining() : L"  next in " + eyes_.remaining();
  }
  swprintf(hud, 512,
    L"BLACK HOLE POMODORO%s\nmode     %s%s\nlevel    -> %.2f\nmotion   %s %s\npresence %s\n",
    rects.size() > 1 ? (L"   (" + std::to_wstring(rects.size()) + L" monitors)").c_str() : L"",
    modeName, phase.c_str(), level,
    cfg_.free.still ? L"still" : L"wander", cfg_.free.still ? L"" : (std::to_wstring(cfg_.free.driftSpeed).substr(0, 4) + L"x").c_str(),
    idle >= 1 ? L"here" : (idle <= 0 ? L"away - faded out" : L"fading"));

  std::lock_guard<std::mutex> lock(snapMu_);
  snap_.hidden = cfg_.hidden; snap_.hud = cfg_.hudVisible; snap_.mode = cfg_.mode;
  snap_.level = level;
  snap_.still = cfg_.free.still; snap_.pinned = { cfg_.free.center[0], cfg_.free.center[1] };
  snap_.driftBase = driftTime_; snap_.driftSpeed = cfg_.hidden ? 0 : cfg_.free.driftSpeed; snap_.driftEpoch = now();
  snap_.centring = cfg_.mode == Mode::EyeBreak ? eyes_.centring() : 0;
  snap_.virt = virtualBounds(rects);
  for (int i = 0; i < LOOK_COUNT; i++) snap_.look[i] = cfg_.look[i];
  snap_.hudText = hud;
}

void App::persist() { if (!testRun_) saveConfig(configFile_, cfg_); }

void App::setLevel(float v) {
  if (cfg_.mode == Mode::Pomodoro) return;       // the clock owns it there
  cfg_.free.level = std::max(0.f, std::min(1.f, v));
  pushState(); persist(); refreshTray();
}
void App::setMode(Mode m) {
  cfg_.mode = m;
  if (m == Mode::Pomodoro) clock_.start(); else clock_.pause();
  if (m == Mode::EyeBreak) eyes_.skip();
  pushState(); persist(); refreshTray();
}
void App::setHidden(bool v) { cfg_.hidden = v; pushState(); persist(); refreshTray(); }
void App::applyPreset(const std::wstring& name) {
  for (int i = 0; i < kPresetCount; i++) if (name == kPresets[i].name) {
    cfg_.preset = name;
    for (int k = 0; k < 14; k++) cfg_.look[k] = kPresets[i].look[k];
    pushState(); persist(); refreshTray(); return;
  }
}
void App::pinAt(float x, float y) { cfg_.free.center[0] = x; cfg_.free.center[1] = y; cfg_.free.still = true; pushState(); persist(); refreshTray(); }
void App::pinAtCursor() {
  POINT pt; GetCursorPos(&pt);
  RECT v; { std::lock_guard<std::mutex> lock(snapMu_); v = snap_.virt; }
  pinAt(std::max(0.f, std::min(1.f, (pt.x - v.left) / (float)(v.right - v.left))),
        std::max(0.f, std::min(1.f, (pt.y - v.top) / (float)(v.bottom - v.top))));
}
void App::setAutostart(bool on) {
  cfg_.autostart = on;
  HKEY key;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) == ERROR_SUCCESS) {
    if (on) { const std::wstring v = L"\"" + exePath() + L"\""; RegSetValueExW(key, L"BlackHolePomodoro", 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); }
    else RegDeleteValueW(key, L"BlackHolePomodoro");
    RegCloseKey(key);
  }
  persist(); refreshTray();
}

// ------------------------------------------------------------------- tick -----
void App::tick() {
  const double t = now(), dt = t - lastTick_; lastTick_ = t;
  if (!cfg_.hidden) driftTime_ += dt * cfg_.free.driftSpeed;
  bool trayDirty = false;
  if (cfg_.mode == Mode::Pomodoro) {
    const auto before = std::make_pair(clock_.phase, clock_.running);
    if (!(cfg_.pomodoro.pauseWhenIdle && idleFactor() <= 0)) clock_.tick(dt);
    if (std::make_pair(clock_.phase, clock_.running) != before) trayDirty = true;
  } else if (cfg_.mode == Mode::EyeBreak) {
    const auto before = eyes_.phase;
    if (!(cfg_.eyebreak.pauseWhenIdle && idleFactor() <= 0)) eyes_.tick(dt);
    if (eyes_.phase != before) trayDirty = true;
  }
  pushState();
  // Tooltip and dimmed icon once a second, like the Electron build.
  static double lastTray = 0;
  if (trayDirty) refreshTray();
  else if (t - lastTray >= 1.0) {
    lastTray = t;
    const std::wstring tip = L"Black Hole Pomodoro - " + statusLine();
    if (tip != lastTip_ && trayAdded_) {
      lastTip_ = tip;
      NOTIFYICONDATAW nid{ sizeof(nid) }; nid.hWnd = hwnd_; nid.uID = 1; nid.uFlags = NIF_TIP;
      wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE); Shell_NotifyIconW(NIM_MODIFY, &nid);
    }
  }
}

// ------------------------------------------------------------------- tray -----
std::wstring App::statusLine() {
  if (cfg_.hidden) return L"Hidden";
  if (cfg_.mode == Mode::Free) return L"Free · size " + std::to_wstring((int)std::lround(cfg_.free.level * 100)) + L"%";
  if (cfg_.mode == Mode::EyeBreak) {
    if (eyes_.inBreak()) return L"LOOK AWAY · " + eyes_.remaining();
    return L"Eye break in " + eyes_.remaining() + (eyes_.running ? L"" : L" (paused)");
  }
  if (clock_.phase == Pomodoro::IDLE) return L"Pomodoro · not started";
  return std::wstring(clock_.phase == Pomodoro::FOCUS ? L"Focus" : L"Break") + L" · " + clock_.remaining() + (clock_.running ? L"" : L" (paused)");
}

void App::refreshTray() {
  const bool free = cfg_.mode == Mode::Free;
  const int iconState = cfg_.hidden ? 0 : ((free || clock_.running) ? 2 : 1);
  NOTIFYICONDATAW nid{ sizeof(nid) }; nid.hWnd = hwnd_; nid.uID = 1;
  nid.uFlags = NIF_TIP | NIF_MESSAGE; nid.uCallbackMessage = WM_APP_TRAY;
  lastTip_ = L"Black Hole Pomodoro - " + statusLine();
  wcsncpy_s(nid.szTip, lastTip_.c_str(), _TRUNCATE);
  if (iconState != lastTrayIconState_ || !trayAdded_) {
    lastTrayIconState_ = iconState;
    HICON old = icon_;
    icon_ = trayIcon(32, iconState == 0 ? 0.3f : iconState == 2 ? 1.f : 0.45f);
    nid.uFlags |= NIF_ICON; nid.hIcon = icon_;
    if (!trayAdded_) { trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != 0; nid.uVersion = NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION, &nid); }
    else Shell_NotifyIconW(NIM_MODIFY, &nid);
    if (old) DestroyIcon(old);
  } else Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static void item(HMENU m, UINT id, const std::wstring& text, bool checked = false, bool radio = false, bool enabled = true) {
  AppendMenuW(m, MF_STRING | (checked ? MF_CHECKED : 0) | (enabled ? 0 : MF_GRAYED), id, text.c_str());
  if (radio) { MENUITEMINFOW mi{ sizeof(mi) }; mi.fMask = MIIM_FTYPE; mi.fType = MFT_RADIOCHECK; SetMenuItemInfoW(m, id, FALSE, &mi); }
}
static void sub(HMENU m, HMENU s, const std::wstring& text, bool enabled = true) {
  AppendMenuW(m, MF_POPUP | MF_STRING | (enabled ? 0 : MF_GRAYED), (UINT_PTR)s, text.c_str());
}
static void sep(HMENU m) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); }

void App::showMenu() {
  const bool free = cfg_.mode == Mode::Free, eye = cfg_.mode == Mode::EyeBreak, wander = !cfg_.free.still;
  const bool sizable = cfg_.mode != Mode::Pomodoro && !cfg_.hidden;
  const size_t monitors = rects_.size();
  HMENU m = CreatePopupMenu();
  item(m, 0, statusLine() + (monitors > 1 ? L"  ·  " + std::to_wstring(monitors) + L" monitors" : L""), false, false, false);
  sep(m);
  item(m, ID_HIDE, L"Hide everything", cfg_.hidden);
  sep(m);
  item(m, ID_MODE_FREE, L"Free mode", free, true, !cfg_.hidden);
  item(m, ID_MODE_POMO, L"Pomodoro mode", cfg_.mode == Mode::Pomodoro, true, !cfg_.hidden);
  item(m, ID_MODE_EYE, L"Eye breaks (20-20-20)", eye, true, !cfg_.hidden);
  sep(m);
  if (eye) {
    item(m, ID_EYE_TOGGLE, eyes_.running ? L"Pause eye breaks" : L"Resume eye breaks", false, false, !cfg_.hidden);
    item(m, ID_EYE_BREAKNOW, eyes_.inBreak() ? L"End this break now" : L"Take a break now", false, false, !cfg_.hidden);
    item(m, 0, L"Breaks taken: " + std::to_wstring(eyes_.completed), false, false, false);
  } else {
    const bool idle = clock_.phase == Pomodoro::IDLE;
    item(m, ID_POMO_TOGGLE, clock_.running ? L"Pause" : (idle ? L"Start focus" : L"Resume"), false, false, !free && !cfg_.hidden);
    item(m, ID_POMO_SKIP, std::wstring(L"Skip to ") + (clock_.phase == Pomodoro::FOCUS ? L"break" : L"focus"), false, false, !free && !cfg_.hidden && !idle);
    item(m, ID_POMO_RESET, L"Reset", false, false, !free && !cfg_.hidden && !idle);
    item(m, 0, L"Completed: " + std::to_wstring(clock_.completed), false, false, false);
  }
  sep(m);
  HMENU size = CreatePopupMenu();
  for (int i = 0; i < 5; i++) item(size, ID_SIZE + i, kSizes[i].label, sizable && approx(cfg_.free.level, kSizes[i].v), true, sizable);
  if (eye) { sep(size); item(size, 0, L"What it shrinks back down to", false, false, false); }
  sub(m, size, eye ? L"Size between breaks" : L"Size");
  if (eye) {
    HMENU sh = CreatePopupMenu();
    for (int i = 0; i < 4; i++) item(sh, ID_SHRINK + i, kShrink[i].label, approx((float)cfg_.eyebreak.recedeSec, kShrink[i].v, 0.01f), true);
    sub(m, sh, L"Shrink back");
  }
  HMENU mv = CreatePopupMenu();
  item(mv, ID_WANDER, monitors > 1 ? L"Wander across all monitors" : L"Wander across the screen", wander, true);
  sep(mv); item(mv, 0, L"Or keep it still at:", false, false, false);
  bool anyPin = false;
  for (int i = 0; i < 4; i++) {
    const bool on = !wander && approx(cfg_.free.center[0], kPins[i].x, 0.005f) && approx(cfg_.free.center[1], kPins[i].y, 0.005f);
    anyPin |= on; item(mv, ID_PIN + i, kPins[i].label, on, true);
  }
  item(mv, ID_PIN_CURSOR, L"At the cursor", !wander && !anyPin, true);
  sub(m, mv, L"Movement");
  HMENU dr = CreatePopupMenu();
  for (int i = 0; i < 4; i++) item(dr, ID_DRIFT + i, kDrifts[i].label, approx(cfg_.free.driftSpeed, kDrifts[i].v), true);
  sub(m, dr, L"Drift speed", wander);
  HMENU gr = CreatePopupMenu();
  for (int i = 0; i < 3; i++) item(gr, ID_CURVE + i, kCurves[i].label, approx((float)cfg_.pomodoro.growthCurve, kCurves[i].v), true);
  sep(gr); item(gr, 0, L"How the hole grows through a focus block", false, false, false);
  sub(m, gr, L"Growth");
  HMENU lk = CreatePopupMenu();
  for (int i = 0; i < kPresetCount; i++) item(lk, ID_PRESET + i, kPresets[i].name, cfg_.preset == kPresets[i].name, true);
  sub(m, lk, L"Look");
  sep(m);
  item(m, ID_IDLE_FADE, L"Fade when I am away", cfg_.idle.enabled);
  item(m, ID_HUD, L"Show HUD", cfg_.hudVisible);
  item(m, ID_AUTOSTART, L"Start with Windows", cfg_.autostart);
  sep(m);
  item(m, ID_QUIT, L"Quit");

  POINT pt; GetCursorPos(&pt);
  SetForegroundWindow(hwnd_);
  const int cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd_, nullptr);
  PostMessageW(hwnd_, WM_NULL, 0, 0);
  DestroyMenu(m);
  if (cmd) onCommand(cmd);
}

void App::onCommand(int id) {
  if (id >= ID_PRESET && id < ID_PRESET + kPresetCount) { applyPreset(kPresets[id - ID_PRESET].name); return; }
  if (id >= ID_SIZE && id < ID_SIZE + 5) { setLevel(kSizes[id - ID_SIZE].v); return; }
  if (id >= ID_SHRINK && id < ID_SHRINK + 4) { cfg_.eyebreak.recedeSec = kShrink[id - ID_SHRINK].v; eyes_.set(cfg_.eyebreak); pushState(); persist(); refreshTray(); return; }
  if (id >= ID_DRIFT && id < ID_DRIFT + 4) { cfg_.free.driftSpeed = kDrifts[id - ID_DRIFT].v; pushState(); persist(); refreshTray(); return; }
  if (id >= ID_PIN && id < ID_PIN + 4) { pinAt(kPins[id - ID_PIN].x, kPins[id - ID_PIN].y); return; }
  if (id >= ID_CURVE && id < ID_CURVE + 3) { cfg_.pomodoro.growthCurve = kCurves[id - ID_CURVE].v; clock_.set(cfg_.pomodoro); pushState(); persist(); refreshTray(); return; }
  switch (id) {
    case ID_HIDE: setHidden(!cfg_.hidden); break;
    case ID_MODE_FREE: setMode(Mode::Free); break;
    case ID_MODE_POMO: setMode(Mode::Pomodoro); break;
    case ID_MODE_EYE: setMode(Mode::EyeBreak); break;
    case ID_EYE_TOGGLE: eyes_.toggle(); pushState(); refreshTray(); break;
    case ID_EYE_BREAKNOW: eyes_.inBreak() ? eyes_.skip() : eyes_.breakNow(); pushState(); refreshTray(); break;
    case ID_POMO_TOGGLE: clock_.toggle(); pushState(); refreshTray(); break;
    case ID_POMO_SKIP: clock_.skip(); pushState(); refreshTray(); break;
    case ID_POMO_RESET: clock_.reset(); pushState(); refreshTray(); break;
    case ID_WANDER: cfg_.free.still = false; pushState(); persist(); refreshTray(); break;
    case ID_PIN_CURSOR: pinAtCursor(); break;
    case ID_IDLE_FADE: cfg_.idle.enabled = !cfg_.idle.enabled; pushState(); persist(); refreshTray(); break;
    case ID_HUD: cfg_.hudVisible = !cfg_.hudVisible; pushState(); persist(); refreshTray(); break;
    case ID_AUTOSTART: setAutostart(!cfg_.autostart); break;
    case ID_QUIT: quit(); break;
  }
}

void App::onHotkey(int id) {
  const auto nudge = [&](float d) { setLevel(cfg_.free.level + d); };
  switch (id) {
    case HK_QUIT: quit(); break;
    case HK_HIDE: setHidden(!cfg_.hidden); break;
    case HK_HUD: cfg_.hudVisible = !cfg_.hudVisible; pushState(); persist(); refreshTray(); break;
    case HK_UP: case HK_RBRACKET: nudge(+0.05f); break;
    case HK_DOWN: case HK_LBRACKET: nudge(-0.05f); break;
    case HK_ZERO: setLevel(0); break;
    case HK_ONE: setLevel(1); break;
    case HK_MODE: setMode(cfg_.mode == Mode::Free ? Mode::Pomodoro : cfg_.mode == Mode::Pomodoro ? Mode::EyeBreak : Mode::Free); break;
    case HK_BREAK: if (cfg_.mode == Mode::EyeBreak) { eyes_.breakNow(); pushState(); refreshTray(); } break;
    case HK_START: if (cfg_.mode == Mode::Pomodoro) { clock_.toggle(); pushState(); refreshTray(); } break;
    case HK_PIN: if (cfg_.free.still) { cfg_.free.still = false; pushState(); persist(); refreshTray(); } else pinAtCursor(); break;
  }
}

void App::smoke(const std::wstring& line) {
  if (!smokeOn_) return;
  static std::mutex mu; std::lock_guard<std::mutex> lock(mu);
  std::wofstream f(L"smoke.txt", std::ios::app); f << line << L"\n";
}

void App::quit() {
  running_ = false;
  if (render_.joinable()) render_.join();
  for (const auto& k : kHotkeys) UnregisterHotKey(hwnd_, k.id);
  if (trayAdded_) { NOTIFYICONDATAW nid{ sizeof(nid) }; nid.hWnd = hwnd_; nid.uID = 1; Shell_NotifyIconW(NIM_DELETE, &nid); trayAdded_ = false; }
  persist();
  { std::lock_guard<std::mutex> lock(overlaysMu_); overlays_.clear(); }
  PostQuitMessage(0);
}

// --------------------------------------------------------------- messages -----
LRESULT CALLBACK App::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  App* self;
  if (m == WM_NCCREATE) { self = (App*)((CREATESTRUCTW*)l)->lpCreateParams; SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self); }
  else self = (App*)GetWindowLongPtrW(h, GWLP_USERDATA);
  return self ? self->handle(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::handle(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_APP_TRAY:
      if (LOWORD(l) == WM_CONTEXTMENU || LOWORD(l) == NIN_SELECT || LOWORD(l) == WM_LBUTTONUP || LOWORD(l) == WM_RBUTTONUP) showMenu();
      return 0;
    case WM_HOTKEY: onHotkey((int)w); return 0;
    case WM_TIMER:
      switch (w) {
        case TIMER_TICK:
          if (g_displayChanged) { g_displayChanged = false; SetTimer(h, TIMER_REBUILD, 400, nullptr); }
          tick(); return 0;
        case TIMER_REBUILD:
          KillTimer(h, TIMER_REBUILD); buildOverlays(); pushState(); refreshTray(); return 0;
        case 10: KillTimer(h, 10); setHidden(true); return 0;
        case 11: KillTimer(h, 11); setHidden(false); return 0;
        case 12: KillTimer(h, 12); { std::lock_guard<std::mutex> lock(snapMu_); snap_.restartCapture = true; } return 0;
        case 13: KillTimer(h, 13); quit(); return 0;
      }
      return 0;
    case WM_DISPLAYCHANGE: g_displayChanged = true; return 0;
    case WM_POWERBROADCAST:
      if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) { std::lock_guard<std::mutex> lock(snapMu_); snap_.restartCapture = true; }
      return TRUE;
    case WM_WTSSESSION_CHANGE:
      if (w == WTS_SESSION_UNLOCK) { std::lock_guard<std::mutex> lock(snapMu_); snap_.restartCapture = true; }
      return 0;
    case WM_CLOSE: quit(); return 0;
    case WM_DESTROY: return 0;
  }
  if (m == g_taskbarCreated && g_taskbarCreated) { trayAdded_ = false; refreshTray(); return 0; }
  return DefWindowProcW(h, m, w, l);
}

// ------------------------------------------------------------ render thread -----
// Bounding box of everything the shader can leave visible for this hole: the far-field
// lens falls off as exp(-(d/7rh)^2) and is invisible past about 14 shadow radii
// (measured by tools/still: coverage stops at ~12-13 rh).
static const float kLensReach = 14.f;
static RECT lensRect(UV winUV, float level, int w, int h, const float* look) {
  const float aspect = (float)w / h;
  const float g = std::pow(std::max(0.f, std::min(1.f, level)), look[TOKEN_EASE]);
  const float rhMin = std::sqrt(look[TOKEN_AREA_MIN] * aspect / 3.1415927f);
  const float rhMax = std::sqrt(look[TOKEN_AREA_MAX] * aspect / 3.1415927f);
  const float rh = (rhMin + (rhMax - rhMin) * g) * (look[HOLE_RADIUS] / 0.08f) * h;   // px
  const float R = kLensReach * rh + 16;
  const float cx = winUV.x * w, cy = winUV.y * h;
  return { (LONG)std::max(0.f, std::floor(cx - R)), (LONG)std::max(0.f, std::floor(cy - R)),
           (LONG)std::min((float)w, std::ceil(cx + R)), (LONG)std::min((float)h, std::ceil(cy + R)) };
}
static RECT unite(const RECT& a, const RECT& b) {
  if (a.right <= a.left || a.bottom <= a.top) return b;
  if (b.right <= b.left || b.bottom <= b.top) return a;
  return { std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right), std::max(a.bottom, b.bottom) };
}

void App::renderLoop() {
  DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
  double refreshHz = 60;
  if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.rateRefresh.uiDenominator)
    refreshHz = (double)ti.rateRefresh.uiNumerator / ti.rateRefresh.uiDenominator;
  const UINT frameMs = (UINT)std::max(1.0, std::floor(1000.0 / refreshHz));

  float shownLevel = 0; UV shownCenter{ 0.5f, 0.35f }; bool haveCenter = false;
  RECT prev[8]{}; bool wasDrawn[8]{};
  bool hudWasOn = false, wasHidden = false, captureWasLive[8]{};
  double last = now(), tStat = last, nextAnim = last;
  int frames = 0, newFrames = 0, echoes = 0, iters = 0, skipped = 0; double cpuMs = 0, waitMs = 0, acqMs = 0, acqAsked = 0; float dirtyPct = 0;
  bool pendingDraw = false;
  // Redraws happen when the desktop really changed (right away, that is the latency
  // that matters) or on the animation clock; never merely because our own present came
  // back through the duplication. A still hole over a still desktop costs nothing.
  const double kAnimHz = 60;
  while (running_) {
    std::lock_guard<std::mutex> lock(overlaysMu_);
    if (overlays_.empty()) { Sleep(50); continue; }
    Snapshot s;
    { std::lock_guard<std::mutex> sl(snapMu_); s = snap_; snap_.restartCapture = false; }
    if (s.restartCapture) { for (auto& o : overlays_) o->restartCapture(); smoke(L"capture restart requested"); }

    // Hidden: no capture, no presents, nothing on the vsync clock - it must be able to
    // sit like this for hours.
    if (s.hidden) {
      for (size_t i = 0; i < overlays_.size(); i++) {
        auto& o = *overlays_[i];
        if (o.captureLive()) { o.stopCapture(); smoke(L"capture stopped"); }
        if (o.takeNeedsBlank()) { o.presentBlank(); wasDrawn[i] = false; }
        captureWasLive[i] = false;
      }
      wasHidden = true; Sleep(100); continue;
    }
    if (wasHidden) { wasHidden = false; for (auto& o : overlays_) o->restartCapture(); }

    // Pacing. Animating: wake on the compositor clock, poll the duplication, and draw when
    // a real frame arrived or the animation clock is due. Idle: block on the duplication -
    // only a real desktop change is worth waking for. Never block on the swapchain's
    // latency waitable: DWM consumes small presents lazily on a static desktop (25-50 ms).
    const double tPre = now(); iters++;
    const float vAspect = (float)(s.virt.right - s.virt.left) / std::max(1L, s.virt.bottom - s.virt.top);
    UV target = s.still ? s.pinned : wanderUV(s.driftBase + (tPre - s.driftEpoch) * s.driftSpeed, shownLevel, vAspect);
    if (s.centring > 0) target = { target.x + (0.5f - target.x) * s.centring, target.y + (0.45f - target.y) * s.centring };
    const bool settled = std::fabs(s.level - shownLevel) < 1e-4f && std::fabs(target.x - shownCenter.x) < 1e-5f &&
                         std::fabs(target.y - shownCenter.y) < 1e-5f;
    const bool animating = !settled || (!s.still && s.driftSpeed != 0) || s.look[STAR_GAIN] > 0 || s.hud;
    if (animating && !overlays_[0]->captureLive()) Sleep(5);
    else if (animating && DCompositionWaitForCompositorClock(0, nullptr, 20) == WAIT_FAILED) Sleep(frameMs);
    waitMs += (now() - tPre) * 1000;
    const UINT timeout = animating ? 0 : 50;
    bool real = false;
    static int metaLogged = 0;
    const double ta0 = now();
    for (size_t i = 0; i < overlays_.size(); i++) {
      const Overlay::Frame f = overlays_[i]->acquire(i == 0 ? timeout : 0);
      if (i == 0) { acqMs += (now() - ta0) * 1000; acqAsked += timeout; }
      if (f != Overlay::NoFrame) newFrames++;
      if (f == Overlay::Echo) echoes++;
      if (f == Overlay::Real) real = true;
      if (f != Overlay::NoFrame && smokeOn_ && metaLogged < 12) { metaLogged++; overlays_[i]->setDebug(metaLogged < 12); smoke(overlays_[i]->lastMeta()); }
    }
    for (size_t i = 0; i < overlays_.size() && i < 8; i++) {
      const bool live = overlays_[i]->captureLive();
      if (live && !captureWasLive[i]) smoke(L"capture started: " + overlays_[i]->name());
      captureWasLive[i] = live;
    }

    const double t = now(), dt = t - last;
    if (!s.still) target = wanderUV(s.driftBase + (t - s.driftEpoch) * s.driftSpeed, shownLevel, vAspect);
    if (s.centring > 0 && !s.still) target = { target.x + (0.5f - target.x) * s.centring, target.y + (0.45f - target.y) * s.centring };
    const bool animDue = t >= nextAnim && animating;
    pendingDraw = pendingDraw || real || s.hud != hudWasOn;
    if (!pendingDraw && !animDue) continue;
    // Previous present not consumed yet: keep the change pending rather than queue behind it.
    if (WaitForSingleObject(overlays_[0]->waitable(), 0) != WAIT_OBJECT_0) { skipped++; continue; }
    pendingDraw = false;
    if (animDue) nextAnim = t + 1.0 / kAnimHz;
    last = t;
    const double t0 = t;
    // Time-based smoothing tuned to the Electron build's per-frame factors at 60 Hz.
    const float kL = 1 - (float)std::exp(-dt / 0.13), kC = 1 - (float)std::exp(-dt / 0.20);
    shownLevel += (s.level - shownLevel) * kL;
    if (!haveCenter) { shownCenter = target; haveCenter = true; }
    else { shownCenter.x += (target.x - shownCenter.x) * kC; shownCenter.y += (target.y - shownCenter.y) * kC; }
    const float iTime = (float)(t - startTime_);
    const double driftNow = s.driftBase + (t - s.driftEpoch) * s.driftSpeed;

    for (size_t i = 0; i < overlays_.size() && i < 8; i++) {
      Overlay& o = *overlays_[i];
      if (o.takeNeedsBlank()) { o.presentBlank(); wasDrawn[i] = false; }
      if (!o.haveFrame()) continue;
      const int w = o.width(), h = o.height();
      const UV winUV = toWindowUV(shownCenter, o.rect(), s.virt);
      const bool shading = shownLevel > 0.002f && shouldShade(winUV, shownLevel);
      const bool hudOn = s.hud && i == 0;
      if (!shading && !hudOn) {
        if (wasDrawn[i]) { o.presentBlank(); wasDrawn[i] = false; }
        continue;
      }
      Uniforms u; defaultUniforms(u);
      for (int k = 0; k < LOOK_COUNT; k++) u.look[k] = s.look[k];
      u.iResolution[0] = (float)w; u.iResolution[1] = (float)h;
      u.iTime = iTime; u.TOKEN_LEVEL = shownLevel;
      u.uCenter[0] = winUV.x; u.uCenter[1] = winUV.y; u.uCenterPin = 1;
      u.uDriftTime = (float)driftNow;

      RECT cur = shading ? lensRect(winUV, shownLevel, w, h, u.look) : RECT{ 0, 0, 0, 0 };
      RECT dirty = unite(cur, wasDrawn[i] ? prev[i] : RECT{ 0, 0, 0, 0 });
      if (hudOn) {
        if (Hud* hud = o.hud()) {
          wchar_t stats[160];
          swprintf(stats, 160, L"fps      %.0f   capture %d/s   render %.2f ms   dirty %.0f%%\n\nctrl+alt  Up/Down size  P mode  S start\n          X hide  K pin  H hud  Q quit",
                   frames / std::max(0.001, t - tStat), (int)(newFrames / std::max(0.001, t - tStat)), frames ? cpuMs / frames : 0.0, dirtyPct);
          hud->setText(s.hudText + stats);
          dirty = unite(dirty, hud->rect());
        }
      } else if (hudWasOn && i == 0 && o.hud()) dirty = unite(dirty, o.hud()->rect());
      o.draw(u, shading ? &cur : nullptr, dirty, hudOn);
      prev[i] = unite(cur, hudOn && o.hud() ? o.hud()->rect() : RECT{ 0, 0, 0, 0 });
      wasDrawn[i] = true;
      if (i == 0) dirtyPct = 100.f * (dirty.right - dirty.left) * (dirty.bottom - dirty.top) / ((float)w * h);
    }
    hudWasOn = s.hud;
    cpuMs += (now() - t0) * 1000; frames++;

    if (t - tStat >= 1.0) {
      wchar_t line[200];
      swprintf(line, 200, L"fps %.1f | capture %.1f/s (%d echo) | render %.2f ms | dirty %.1f%% | level %.2f | uv %.2f,%.2f%s"
               L" | iters %d skipped %d wait %.1f ms acq %.1f ms",
               frames / (t - tStat), newFrames / (t - tStat), echoes, frames ? cpuMs / frames : 0.0, dirtyPct, shownLevel,
               shownCenter.x, shownCenter.y, overlays_[0]->captureLive() ? L"" : L"  [capture lost]",
               iters, skipped, iters ? waitMs / iters : 0.0, iters ? acqMs / iters : 0.0);
      smoke(line);
      { std::lock_guard<std::mutex> sl(statsMu_); renderStats_ = line; }
      frames = newFrames = echoes = iters = skipped = 0; cpuMs = waitMs = acqMs = acqAsked = 0; tStat = t;
    }
  }
}
