// Step 2 spike: is a native DDA -> D3D11 -> DirectComposition overlay actually tighter
// than the Electron build? Draws a magnified, magenta-ringed circle that wanders slowly,
// and measures the two things that decide go/no-go:
//   - capture latency, by decoding a frame counter drawn in a separate ordinary window
//     back out of our own capture (we are excluded from capture, so the reading is clean)
//   - self-capture, by looking for the ring's magenta in the captured desktop
// Usage: overlay.exe [--seconds N] [--full] [--nometer]      Ctrl+Alt+Q quits.
#include "overlay.h"
#include <dwmapi.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#pragma comment(lib, "dwmapi.lib")

static const char* kSpikeHLSL = R"(
cbuffer P : register(b0) { float2 res; float2 center; float radius; float mag; float ring; float pad; };
Texture2D desk : register(t0); SamplerState smp : register(s0);
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID) {
  VSOut o; float2 p = float2((id << 1) & 2, id & 2);
  o.uv = p; o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); return o;
}
float4 PS(VSOut i) : SV_TARGET {
  float2 px = i.uv * res;
  float2 d = px - center; float r = length(d);
  if (r > radius) return float4(0, 0, 0, 0);
  if (r > radius - ring) return float4(1, 0, 1, 1);
  float2 suv = (center + d / mag) / res;
  return float4(desk.Sample(smp, suv).rgb, 1);
}
)";

// A plain window at the output's origin that draws an 8-bit counter as black/white blocks.
// It is NOT capture-excluded, so the capture shows it a few frames late; that gap is the
// capture latency. Same trick as probes/native, minus the self-capture confound.
struct Meter {
  static const int NBITS = 8, BS = 32;
  HWND hwnd = nullptr;
  ComPtr<IDXGISwapChain1> swap;
  ComPtr<ID3D11RenderTargetView> rtv;
  ID3D11DeviceContext1* ctx = nullptr;
  int drawn = 0;

  bool init(Overlay& o, HINSTANCE hinst) {
    ctx = o.context();
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = hinst; wc.lpszClassName = L"BHPMeter";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"meter", WS_POPUP,
                           o.rect().left, o.rect().top, NBITS * BS, BS, nullptr, nullptr, hinst, nullptr);
    if (!hwnd) return false;
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = NBITS * BS; scd.Height = BS; scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1; scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2; scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(o.factory()->CreateSwapChainForHwnd(o.device(), hwnd, &scd, nullptr, nullptr, &swap))) return false;
    // One frame of queue, so the reading is capture latency and not the meter's own backlog.
    ComPtr<IDXGISwapChain2> s2; if (SUCCEEDED(swap.As(&s2))) s2->SetMaximumFrameLatency(1);
    ComPtr<ID3D11Texture2D> back; swap->GetBuffer(0, IID_PPV_ARGS(&back));
    o.device()->CreateRenderTargetView(back.Get(), nullptr, &rtv);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    return true;
  }
  void tick() {
    drawn = (drawn + 1) & 0xFF;
    for (int i = 0; i < NBITS; i++) {
      float v = ((drawn >> i) & 1) ? 1.f : 0.f; float col[4] = { v, v, v, 1 };
      D3D11_RECT r{ i * BS, 0, i * BS + BS, BS };
      ctx->ClearView(rtv.Get(), col, &r, 1);
    }
    swap->Present(0, 0);
  }
  // Frames between what we last drew and what the capture shows; -1 if unreadable.
  int decode(Overlay& o) {
    unsigned char px[NBITS * BS * 4];
    if (!o.readCapture(0, BS / 2, NBITS * BS, 1, px)) return -1;
    int v = 0;
    for (int i = 0; i < NBITS; i++) {
      int s = px[(i * BS + BS / 2) * 4];
      if (s > 60 && s < 195) return -1;             // something else is covering the strip
      if (s >= 128) v |= 1 << i;
    }
    int d = (drawn - v) & 0xFF;
    return d < 40 ? d : -1;
  }
  ~Meter() { if (hwnd) DestroyWindow(hwnd); }
};

static double seconds() {
  static LARGE_INTEGER f{}; if (!f.QuadPart) QueryPerformanceFrequency(&f);
  LARGE_INTEGER n; QueryPerformanceCounter(&n); return double(n.QuadPart) / f.QuadPart;
}

static std::vector<std::unique_ptr<Overlay>> buildOverlays(HINSTANCE hinst) {
  std::vector<std::unique_ptr<Overlay>> out;
  ComPtr<IDXGIFactory1> f; CreateDXGIFactory1(IID_PPV_ARGS(&f));
  for (UINT ai = 0; ; ai++) {
    ComPtr<IDXGIAdapter1> a;
    if (f->EnumAdapters1(ai, &a) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 ad{}; a->GetDesc1(&ad);
    if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
    // Hybrid laptops: DXGI lists the outputs under whichever GPU Windows assigned this
    // process to, and Desktop Duplication only works on the adapter that really owns
    // the panel. Creating a device on each adapter before enumerating its outputs is
    // what the probe did and what made the outputs show up under the right one.
    { ComPtr<ID3D11Device> pin; D3D_FEATURE_LEVEL fl;
      D3D11CreateDevice(a.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &pin, &fl, nullptr); }
    for (UINT oi = 0; ; oi++) {
      ComPtr<IDXGIOutput> o;
      if (a->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND) { if (oi == 0) wprintf(L"  [%s] no outputs\n", ad.Description); break; }
      DXGI_OUTPUT_DESC od{}; o->GetDesc(&od);
      wprintf(L"  [%s] output %u %s attached=%d\n", ad.Description, oi, od.DeviceName, od.AttachedToDesktop);
      if (!od.AttachedToDesktop) continue;
      ComPtr<IDXGIOutput1> o1; if (FAILED(o.As(&o1))) continue;
      auto ov = std::make_unique<Overlay>(a, o1, hinst);
      std::wstring err;
      if (!ov->init(kSpikeHLSL, &err)) { wprintf(L"  %s on %s: init failed: %s\n", od.DeviceName, ad.Description, err.c_str()); continue; }
      wprintf(L"  overlay on %s [%s]: window %dx%d at (%ld,%ld), capture %dx%d, exclude-from-capture %s\n",
              od.DeviceName, ad.Description, ov->width(), ov->height(), ov->rect().left, ov->rect().top,
              ov->captureWidth(), ov->captureHeight(), ov->affinityOk() ? L"OK" : L"FAILED");
      out.push_back(std::move(ov));
    }
  }
  return out;
}

int wmain(int argc, wchar_t** argv) {
  double runSeconds = 0; bool useDirty = true, useMeter = true; float mag = 1.0f;
  for (int i = 1; i < argc; i++) {
    if (!wcscmp(argv[i], L"--seconds") && i + 1 < argc) runSeconds = _wtof(argv[++i]);
    else if (!wcscmp(argv[i], L"--mag") && i + 1 < argc) mag = (float)_wtof(argv[++i]);
    else if (!wcscmp(argv[i], L"--full")) useDirty = false;
    else if (!wcscmp(argv[i], L"--nometer")) useMeter = false;
  }
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  HINSTANCE hinst = GetModuleHandle(nullptr);
  if (!RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q')) {
    printf("FATAL: could not register Ctrl+Alt+Q (is the Electron build running?)\n"); return 1;
  }

  printf("=== building overlays ===\n");
  auto overlays = buildOverlays(hinst);
  if (overlays.empty()) { printf("no overlays\n"); return 1; }
  std::unique_ptr<Meter> meter;
  if (useMeter) { meter = std::make_unique<Meter>(); if (!meter->init(*overlays[0], hinst)) meter.reset(); }
  // Refresh vs compose rate: a DRR panel can scan at 165 Hz while DWM composes at 60.
  DWM_TIMING_INFO ti{}; ti.cbSize = sizeof(ti);
  double refreshHz = 60;
  if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &ti)) && ti.rateRefresh.uiDenominator) {
    refreshHz = (double)ti.rateRefresh.uiNumerator / ti.rateRefresh.uiDenominator;
    printf("DWM refresh %.1f Hz, compose %.1f Hz\n", refreshHz,
           ti.rateCompose.uiDenominator ? (double)ti.rateCompose.uiNumerator / ti.rateCompose.uiDenominator : 0.0);
  }
  const UINT frameMs = (UINT)max(1.0, floor(1000.0 / refreshHz));
  printf("dirty rects %s, meter %s, mag %.2f. Ctrl+Alt+Q quits.\n\n", useDirty ? "ON" : "OFF", meter ? "ON" : "OFF", mag);

  const double tStart = seconds();
  double tStat = tStart;
  int frames = 0, newFrames = 0, latN = 0, latLast = -1; double latSum = 0, renderMs = 0, dirtyPct = 0;
  bool selfCapture = false;
  RECT prev[8]{}; bool havePrev[8]{};
  bool quit = false;

  while (!quit) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_HOTKEY) quit = true;
      TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    if (g_displayChanged) {
      g_displayChanged = false;
      printf("display change -> rebuilding overlays\n");
      meter.reset(); overlays.clear(); Sleep(400);
      overlays = buildOverlays(hinst);
      if (overlays.empty()) { printf("no overlays after rebuild\n"); return 1; }
      if (useMeter) { meter = std::make_unique<Meter>(); if (!meter->init(*overlays[0], hinst)) meter.reset(); }
      memset(havePrev, 0, sizeof(havePrev));
    }

    // Throttle to one queued frame, then block until the desktop changes - for at most one
    // refresh, so animation keeps running on a static desktop. Presenting right after the
    // frame lands (instead of at the next wake) is what gets the lens to one frame behind.
    WaitForSingleObject(overlays[0]->waitable(), overlays[0]->captureLive() ? 50 : 5);
    if (overlays[0]->acquire(frameMs)) newFrames++;
    const double now = seconds(), t = now - tStart;
    double t0 = now;

    for (size_t i = 0; i < overlays.size() && i < 8; i++) {
      Overlay& o = *overlays[i];
      if (i > 0 && o.acquire()) newFrames++;
      if (o.takeNeedsBlank()) { o.presentBlank(); havePrev[i] = false; }
      if (!o.haveFrame()) continue;

      // slow Lissajous wander so the dirty-rect path is exercised by motion
      const float W = (float)o.width(), H = (float)o.height();
      LensParams p{};
      p.res[0] = W; p.res[1] = H;
      p.radius = 0.22f * H;
      p.center[0] = W * (0.5f + 0.28f * (float)sin(t * 0.21));
      p.center[1] = H * (0.42f + 0.18f * (float)sin(t * 0.17 + 1.3));
      p.mag = mag; p.ring = 5.f;

      RECT cur{ (LONG)floor(p.center[0] - p.radius) - 1, (LONG)floor(p.center[1] - p.radius) - 1,
                (LONG)ceil (p.center[0] + p.radius) + 1, (LONG)ceil (p.center[1] + p.radius) + 1 };
      cur.left = max(0L, cur.left); cur.top = max(0L, cur.top);
      cur.right = min((LONG)o.width(), cur.right); cur.bottom = min((LONG)o.height(), cur.bottom);
      RECT dirty = cur;
      if (havePrev[i]) { dirty.left = min(dirty.left, prev[i].left); dirty.top = min(dirty.top, prev[i].top);
                         dirty.right = max(dirty.right, prev[i].right); dirty.bottom = max(dirty.bottom, prev[i].bottom); }
      o.draw(p, useDirty ? &dirty : nullptr);
      prev[i] = cur; havePrev[i] = true;
      if (i == 0) dirtyPct = 100.0 * (dirty.right - dirty.left) * (dirty.bottom - dirty.top) / (W * H);

      if (i == 0 && frames % 165 == 0) {          // ~once a second: is our own ring in the capture?
        unsigned char px[4];
        int rx = (int)(p.center[0] + p.radius - p.ring * 0.5f), ry = (int)p.center[1];
        if (o.readCapture(rx, ry, 1, 1, px) && px[2] > 200 && px[1] < 60 && px[0] > 200) selfCapture = true;
      }
    }
    renderMs += (seconds() - t0) * 1000.0;

    if (meter && overlays[0]->haveFrame()) {
      if (frames % 10 == 0) { int d = meter->decode(*overlays[0]); if (d >= 0) { latLast = d; latSum += d; latN++; } }
      meter->tick();
    }
    frames++;

    if (now - tStat >= 1.0) {
      double el = now - tStat;
      char cap[64];
      if (overlays[0]->captureLive()) strcpy_s(cap, "live");
      else sprintf_s(cap, "LOST (DuplicateOutput=0x%08lX, retrying)", (unsigned long)overlays[0]->lastDuplicateResult());
      printf("present %6.1f fps | capture %5.1f new/s | cpu render %.2f ms | dirty %4.1f%% | "
             "capture latency last %2d avg %.2f frames (n=%d) | self-capture %s | capture %s\n",
             frames / el, newFrames / el, frames ? renderMs / frames : 0.0, dirtyPct,
             latLast, latN ? latSum / latN : -1.0, latN, selfCapture ? "DETECTED!" : "none", cap);
      fflush(stdout);
      frames = newFrames = 0; renderMs = 0; tStat = now;
    }
    if (runSeconds > 0 && t >= runSeconds) quit = true;
  }
  printf("\nexit. self-capture %s, exclude-from-capture %s, avg capture latency %.2f frames (n=%d)\n",
         selfCapture ? "DETECTED" : "none", overlays[0]->affinityOk() ? "OK" : "FAILED",
         latN ? latSum / latN : -1.0, latN);
  return 0;
}
