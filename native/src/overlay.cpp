#include "overlay.h"
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

volatile bool g_displayChanged = false;
static const wchar_t* kClass = L"BlackHolePomodoroOverlay";

Overlay::Overlay(ComPtr<IDXGIAdapter1> adapter, ComPtr<IDXGIOutput1> output, HINSTANCE hinst)
  : adapter_(adapter), output_(output), hinst_(hinst) {}

Overlay::~Overlay() {
  loseDuplication();
  hud_.reset();
  if (hwnd_) DestroyWindow(hwnd_);
}

LRESULT CALLBACK Overlay::wndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED: g_displayChanged = true; return 0;
    case WM_NCHITTEST: return HTTRANSPARENT;   // belt and braces next to WS_EX_TRANSPARENT
  }
  return DefWindowProcW(h, m, w, l);
}

bool Overlay::init(const std::wstring& hlslPath, std::wstring* err) {
  auto fail = [&](const wchar_t* what, HRESULT hr) {
    wchar_t b[256]; swprintf(b, 256, L"%s failed: 0x%08lX", what, (unsigned long)hr);
    *err = b; return false;
  };
  HRESULT hr;

  DXGI_OUTPUT_DESC od{}; output_->GetDesc(&od);
  DXGI_ADAPTER_DESC1 ad{}; adapter_->GetDesc1(&ad);
  rect_ = od.DesktopCoordinates; name_ = od.DeviceName; luid_ = ad.AdapterLuid;
  const int w = width(), h = height();
  if (FAILED(hr = adapter_->GetParent(IID_PPV_ARGS(&factory_)))) return fail(L"adapter GetParent", hr);

  // ---- device on the adapter that owns this output (DDA requires it) ----
  ComPtr<ID3D11DeviceContext> ctx0; D3D_FEATURE_LEVEL fl;
  hr = D3D11CreateDevice(adapter_.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                         D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
                         &dev_, &fl, &ctx0);
  if (FAILED(hr)) return fail(L"D3D11CreateDevice", hr);
  if (FAILED(hr = ctx0.As(&ctx_))) return fail(L"ID3D11DeviceContext1", hr);

  // ---- window: full monitor, click-through, never activated, invisible to capture ----
  static bool registered = false;
  if (!registered) {
    WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = hinst_; wc.lpszClassName = kClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc); registered = true;
  }
  const DWORD ex = WS_EX_NOREDIRECTIONBITMAP | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
                   WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
  hwnd_ = CreateWindowExW(ex, kClass, L"Black Hole Pomodoro", WS_POPUP,
                          rect_.left, rect_.top, w, h, nullptr, nullptr, hinst_, nullptr);
  if (!hwnd_) return fail(L"CreateWindowEx", HRESULT_FROM_WIN32(GetLastError()));
  SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);   // a layered window is hidden until told otherwise
  affinityOk_ = SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE) != 0;
  scale_ = GetDpiForWindow(hwnd_) / 96.f;

  // ---- composition swapchain, 1 frame of latency ----
  ComPtr<IDXGIDevice> dxgiDev; dev_.As(&dxgiDev);
  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.Width = w; scd.Height = h; scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  scd.SampleDesc.Count = 1; scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = 2; scd.Scaling = DXGI_SCALING_STRETCH;
  scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  scd.Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
  ComPtr<IDXGISwapChain1> sc1;
  if (FAILED(hr = factory_->CreateSwapChainForComposition(dev_.Get(), &scd, nullptr, &sc1)))
    return fail(L"CreateSwapChainForComposition", hr);
  if (FAILED(hr = sc1.As(&swap_))) return fail(L"IDXGISwapChain2", hr);
  swap_->SetMaximumFrameLatency(1);
  waitable_ = swap_->GetFrameLatencyWaitableObject();

  if (FAILED(hr = DCompositionCreateDevice(dxgiDev.Get(), IID_PPV_ARGS(&dcomp_)))) return fail(L"DCompositionCreateDevice", hr);
  if (FAILED(hr = dcomp_->CreateTargetForHwnd(hwnd_, TRUE, &target_))) return fail(L"CreateTargetForHwnd", hr);
  if (FAILED(hr = dcomp_->CreateVisual(&visual_))) return fail(L"CreateVisual", hr);
  visual_->SetContent(swap_.Get());
  target_->SetRoot(visual_.Get());
  if (FAILED(hr = dcomp_->Commit())) return fail(L"DComp Commit", hr);

  ComPtr<ID3D11Texture2D> back;
  if (FAILED(hr = swap_->GetBuffer(0, IID_PPV_ARGS(&back)))) return fail(L"GetBuffer", hr);
  if (FAILED(hr = dev_->CreateRenderTargetView(back.Get(), nullptr, &rtv_))) return fail(L"CreateRenderTargetView", hr);

  std::string rerr;
  if (!renderer_.init(dev_.Get(), hlslPath, &rerr)) {
    *err = L"shader: " + std::wstring(rerr.begin(), rerr.end()); return false;
  }

  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  startDuplication();          // may legitimately fail right now (secure desktop); acquire() retries
  return true;
}

Hud* Overlay::hud() {
  if (!hud_) {
    auto h = std::make_unique<Hud>(); std::string err;
    if (h->init(dev_.Get(), swap_.Get(), scale_, &err)) hud_ = std::move(h);
  }
  return hud_.get();
}

bool Overlay::startDuplication() {
  lastRetry_ = GetTickCount64();
  ComPtr<IDXGIOutputDuplication> d;
  lastDupHr_ = output_->DuplicateOutput(dev_.Get(), &d);
  if (FAILED(lastDupHr_)) return false;
  DXGI_OUTDUPL_DESC dd{}; d->GetDesc(&dd);
  if (!cap_ || (int)dd.ModeDesc.Width != capW_ || (int)dd.ModeDesc.Height != capH_) {
    capW_ = dd.ModeDesc.Width; capH_ = dd.ModeDesc.Height;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = capW_; td.Height = capH_; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    cap_.Reset(); capSrv_.Reset();
    dev_->CreateTexture2D(&td, nullptr, &cap_);
    dev_->CreateShaderResourceView(cap_.Get(), nullptr, &capSrv_);
  }
  dupl_ = d;
  haveFrame_ = false;
  return true;
}

void Overlay::loseDuplication() {
  if (dupl_) { dupl_->ReleaseFrame(); dupl_.Reset(); }
  haveFrame_ = false;
  needBlank_ = true;
}

void Overlay::stopCapture() { loseDuplication(); lastRetry_ = GetTickCount64(); }

bool Overlay::isEcho(const RECT& d) const {
  for (const RECT& p : presented_) {
    if (p.right <= p.left) continue;
    if (d.left >= p.left - 2 && d.top >= p.top - 2 && d.right <= p.right + 2 && d.bottom <= p.bottom + 2) return true;
  }
  return false;
}

Overlay::Frame Overlay::acquire(UINT timeoutMs) {
  if (!dupl_) {
    if (GetTickCount64() - lastRetry_ < 500) { Sleep(timeoutMs); return NoFrame; }
    if (!startDuplication()) return NoFrame;
  }
  DXGI_OUTDUPL_FRAME_INFO fi{}; ComPtr<IDXGIResource> res;
  HRESULT hr = dupl_->AcquireNextFrame(timeoutMs, &fi, &res);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) return NoFrame;
  if (FAILED(hr)) { loseDuplication(); return NoFrame; }
  Frame kind = NoFrame;
  // LastPresentTime == 0 means only the cursor moved; the image itself is unchanged.
  if (fi.LastPresentTime.QuadPart != 0) {
    ComPtr<ID3D11Texture2D> t;
    if (SUCCEEDED(res.As(&t))) {
      ctx_->CopyResource(cap_.Get(), t.Get());
      haveFrame_ = true;
      kind = Real;
      UINT moveBytes = 0, dirtyBytes = 0; RECT first{}; bool echo = false;
      if (fi.TotalMetadataBufferSize) {
        meta_.resize(fi.TotalMetadataBufferSize);
        echo = SUCCEEDED(dupl_->GetFrameMoveRects((UINT)meta_.size(), (DXGI_OUTDUPL_MOVE_RECT*)meta_.data(), &moveBytes)) && moveBytes == 0;
        if (SUCCEEDED(dupl_->GetFrameDirtyRects((UINT)meta_.size(), (RECT*)meta_.data(), &dirtyBytes))) {
          const RECT* rects = (const RECT*)meta_.data();
          if (dirtyBytes) first = rects[0];
          for (UINT i = 0; i < dirtyBytes / sizeof(RECT) && echo; i++) echo = isEcho(rects[i]);
        } else echo = false;
        if (echo) kind = Echo;
      }
      if (debug_) {
        wchar_t b[256];
        const RECT& lp = presented_[(presentedIdx_ + 2) % 3];
        swprintf(b, 256, L"frame: meta %u B, moves %u, dirty %u, first (%ld,%ld)-(%ld,%ld), presented (%ld,%ld)-(%ld,%ld), acc %u -> %s",
                 fi.TotalMetadataBufferSize, moveBytes / (UINT)sizeof(DXGI_OUTDUPL_MOVE_RECT), dirtyBytes / (UINT)sizeof(RECT),
                 first.left, first.top, first.right, first.bottom, lp.left, lp.top, lp.right, lp.bottom,
                 fi.AccumulatedFrames, kind == Echo ? L"echo" : L"real");
        lastMeta_ = b;
      }
    }
  }
  dupl_->ReleaseFrame();
  return kind;
}

void Overlay::draw(const Uniforms& u, const RECT* scissor, const RECT& dirty, bool withHud) {
  const int w = width(), h = height();
  const float clear[4] = { 0, 0, 0, 0 };
  D3D11_RECT d{ dirty.left, dirty.top, dirty.right, dirty.bottom };
  ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
  ctx_->ClearView(rtv_.Get(), clear, &d, 1);
  if (scissor) {
    D3D11_RECT s{ scissor->left, scissor->top, scissor->right, scissor->bottom };
    renderer_.draw(ctx_.Get(), rtv_.Get(), capSrv_.Get(), u, w, h, &s);
  }
  if (withHud && hud()) hud_->draw();

  // The first present of a flip chain must be a full one; after that DWM only touches
  // the dirty rect, which is what keeps a small hole cheap on a big screen.
  DXGI_PRESENT_PARAMETERS pp{};
  RECT dr = dirty;
  if (!firstPresent_) { pp.DirtyRectsCount = 1; pp.pDirtyRects = &dr; }
  swap_->Present1(1, 0, &pp);
  notePresented(firstPresent_ ? RECT{ 0, 0, w, h } : dirty);
  firstPresent_ = false;
}

void Overlay::presentBlank() {
  const float clear[4] = { 0, 0, 0, 0 };
  ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
  ctx_->ClearRenderTargetView(rtv_.Get(), clear);
  DXGI_PRESENT_PARAMETERS pp{};
  swap_->Present1(1, 0, &pp);
  notePresented(RECT{ 0, 0, width(), height() });
  firstPresent_ = false;
}
