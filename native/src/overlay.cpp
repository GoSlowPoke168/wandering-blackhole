#include "overlay.h"
#include <d3dcompiler.h>
#include <cstdio>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

volatile bool g_displayChanged = false;
static const wchar_t* kClass = L"BlackHolePomodoroOverlay";

Overlay::Overlay(ComPtr<IDXGIAdapter1> adapter, ComPtr<IDXGIOutput1> output, HINSTANCE hinst)
  : adapter_(adapter), output_(output), hinst_(hinst) {}

Overlay::~Overlay() {
  loseDuplication();
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

bool Overlay::init(const char* hlsl, std::wstring* err) {
  auto fail = [&](const wchar_t* what, HRESULT hr) {
    wchar_t b[256]; swprintf(b, 256, L"%s failed: 0x%08lX", what, (unsigned long)hr);
    *err = b; return false;
  };
  HRESULT hr;

  DXGI_OUTPUT_DESC od{}; output_->GetDesc(&od);
  rect_ = od.DesktopCoordinates; name_ = od.DeviceName;
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

  // ---- pipeline ----
  ComPtr<ID3DBlob> vsb, psb, e;
  if (FAILED(D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsb, &e)) ||
      FAILED(D3DCompile(hlsl, strlen(hlsl), nullptr, nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psb, &e))) {
    printf("shader compile failed:\n%s\n", e ? (const char*)e->GetBufferPointer() : "?");
    return fail(L"D3DCompile", E_FAIL);
  }
  dev_->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_);
  dev_->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_);
  D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  dev_->CreateSamplerState(&sd, &sampler_);
  D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
  rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
  dev_->CreateRasterizerState(&rd, &raster_);
  D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(LensParams); bd.Usage = D3D11_USAGE_DEFAULT;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  dev_->CreateBuffer(&bd, nullptr, &cbuf_);

  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  startDuplication();          // may legitimately fail right now (secure desktop); acquire() retries
  return true;
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

bool Overlay::acquire(UINT timeoutMs) {
  if (!dupl_) {
    if (GetTickCount64() - lastRetry_ < 500) { Sleep(timeoutMs); return false; }
    if (!startDuplication()) return false;
  }
  DXGI_OUTDUPL_FRAME_INFO fi{}; ComPtr<IDXGIResource> res;
  HRESULT hr = dupl_->AcquireNextFrame(timeoutMs, &fi, &res);
  if (hr == DXGI_ERROR_WAIT_TIMEOUT) return false;
  if (FAILED(hr)) { loseDuplication(); return false; }
  bool fresh = false;
  // LastPresentTime == 0 means only the cursor moved; the image itself is unchanged.
  if (fi.LastPresentTime.QuadPart != 0) {
    ComPtr<ID3D11Texture2D> t;
    if (SUCCEEDED(res.As(&t))) { ctx_->CopyResource(cap_.Get(), t.Get()); haveFrame_ = true; fresh = true; }
  }
  dupl_->ReleaseFrame();
  return fresh;
}

void Overlay::draw(const LensParams& p, const RECT* dirty) {
  const int w = width(), h = height();
  const float clear[4] = { 0, 0, 0, 0 };
  D3D11_RECT r = dirty ? D3D11_RECT{ dirty->left, dirty->top, dirty->right, dirty->bottom }
                       : D3D11_RECT{ 0, 0, w, h };
  ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
  D3D11_VIEWPORT vp{ 0, 0, (float)w, (float)h, 0, 1 };
  ctx_->RSSetViewports(1, &vp);
  if (dirty) ctx_->ClearView(rtv_.Get(), clear, &r, 1);
  else       ctx_->ClearRenderTargetView(rtv_.Get(), clear);
  ctx_->RSSetScissorRects(1, &r);
  ctx_->RSSetState(raster_.Get());
  ctx_->UpdateSubresource(cbuf_.Get(), 0, nullptr, &p, 0, 0);
  ctx_->VSSetShader(vs_.Get(), nullptr, 0);
  ctx_->PSSetShader(ps_.Get(), nullptr, 0);
  ctx_->PSSetConstantBuffers(0, 1, cbuf_.GetAddressOf());
  ctx_->PSSetShaderResources(0, 1, capSrv_.GetAddressOf());
  ctx_->PSSetSamplers(0, 1, sampler_.GetAddressOf());
  ctx_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx_->Draw(3, 0);

  // The first present of a flip chain must be a full one; after that DWM only touches
  // the dirty rect, which is what keeps a small hole cheap on a big screen.
  DXGI_PRESENT_PARAMETERS pp{};
  RECT dr = dirty ? *dirty : RECT{ 0, 0, w, h };
  if (dirty && !firstPresent_) { pp.DirtyRectsCount = 1; pp.pDirtyRects = &dr; }
  swap_->Present1(1, 0, &pp);
  firstPresent_ = false;
}

void Overlay::presentBlank() {
  const float clear[4] = { 0, 0, 0, 0 };
  ctx_->OMSetRenderTargets(1, rtv_.GetAddressOf(), nullptr);
  ctx_->ClearRenderTargetView(rtv_.Get(), clear);
  DXGI_PRESENT_PARAMETERS pp{};
  swap_->Present1(1, 0, &pp);
  firstPresent_ = false;
}

bool Overlay::readCapture(int x, int y, int w, int h, unsigned char* out) {
  if (!cap_ || x < 0 || y < 0 || x + w > capW_ || y + h > capH_) return false;
  if (!staging_ || stagingW_ != w || stagingH_ != h) {
    D3D11_TEXTURE2D_DESC td{};
    td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_STAGING; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_.Reset();
    if (FAILED(dev_->CreateTexture2D(&td, nullptr, &staging_))) return false;
    stagingW_ = w; stagingH_ = h;
  }
  D3D11_BOX box{ (UINT)x, (UINT)y, 0, (UINT)(x + w), (UINT)(y + h), 1 };
  ctx_->CopySubresourceRegion(staging_.Get(), 0, 0, 0, 0, cap_.Get(), 0, &box);
  D3D11_MAPPED_SUBRESOURCE ms{};
  if (FAILED(ctx_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &ms))) return false;
  for (int row = 0; row < h; row++)
    memcpy(out + row * w * 4, (const unsigned char*)ms.pData + row * ms.RowPitch, w * 4);
  ctx_->Unmap(staging_.Get(), 0);
  return true;
}
