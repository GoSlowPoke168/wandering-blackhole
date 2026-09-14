// THROWAWAY - Milestone 0 capture probe (native ceiling).
// DXGI Desktop Duplication -> D3D11 SRV -> passthrough PS -> swapchain.
// Like Probe A it does NOT exclude itself from capture: it captures its own
// counter blocks, and the round-trip delay IS the end-to-end latency.
// Usage: probeB.exe [seconds]   (default 20). Esc also quits.
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#pragma comment(lib,"d3d11.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib,"user32.lib")

using Microsoft::WRL::ComPtr;
static const int NBITS = 8, BS = 32;
static bool g_quit = false;

static const char* kHLSL =
"struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"VSOut VS(uint id : SV_VertexID) {\n"
"  VSOut o; float2 p = float2((id << 1) & 2, id & 2);\n"
"  o.uv = p; o.pos = float4(p * float2(2,-2) + float2(-1,1), 0, 1); return o;\n"
"}\n"
"Texture2D tex : register(t0); SamplerState smp : register(s0);\n"
"float4 PS(VSOut i) : SV_TARGET { return float4(tex.Sample(smp, i.uv).rgb, 1); }\n";

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == WM_DESTROY || (m == WM_KEYDOWN && w == VK_ESCAPE)) { g_quit = true; PostQuitMessage(0); return 0; }
  return DefWindowProc(h, m, w, l);
}

#define HR(x) do { HRESULT _hr=(x); if(FAILED(_hr)){ printf("FAIL %s = 0x%08lX\n", #x, (unsigned long)_hr); return 1; } } while(0)

int main(int argc, char** argv) {
  const double RUN_SECONDS = argc > 1 ? atof(argv[1]) : 20.0;
  const UINT SYNC = (argc > 2 && !strcmp(argv[2], "vsync")) ? 1 : 0;
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const int W = GetSystemMetrics(SM_CXSCREEN), H = GetSystemMetrics(SM_CYSCREEN);

  ComPtr<IDXGIFactory1> factory1;
  HR(CreateDXGIFactory1(IID_PPV_ARGS(&factory1)));

  // On hybrid-graphics laptops the desktop is composited by whichever adapter owns
  // the output (usually the iGPU). Probe every adapter/output pair and keep the
  // first that actually duplicates - and report which one that was.
  ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx;
  ComPtr<IDXGIOutput1> out1; ComPtr<IDXGIOutputDuplication> dupl;
  ComPtr<IDXGIAdapter1> adapter; DXGI_ADAPTER_DESC1 chosen{}; int chosenOut = -1;

  printf("=== enumerating adapters ===\n");
  for (UINT ai = 0; ; ai++) {
    ComPtr<IDXGIAdapter1> a;
    if (factory1->EnumAdapters1(ai, &a) == DXGI_ERROR_NOT_FOUND) break;
    DXGI_ADAPTER_DESC1 ad{}; a->GetDesc1(&ad);
    wprintf(L"  [%u] %s (%.0f MB dedicated)\n", ai, ad.Description, ad.DedicatedVideoMemory / 1048576.0);
    if (ad.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { printf("      (software, skipped)\n"); continue; }

    ComPtr<ID3D11Device> d; ComPtr<ID3D11DeviceContext> c; D3D_FEATURE_LEVEL fl;
    if (FAILED(D3D11CreateDevice(a.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0,
                                 D3D11_SDK_VERSION, &d, &fl, &c))) {
      printf("      device creation failed\n"); continue;
    }
    for (UINT oi = 0; ; oi++) {
      ComPtr<IDXGIOutput> o;
      if (a->EnumOutputs(oi, &o) == DXGI_ERROR_NOT_FOUND) { if (oi == 0) printf("      no outputs\n"); break; }
      DXGI_OUTPUT_DESC od{}; o->GetDesc(&od);
      ComPtr<IDXGIOutput1> o1;
      if (FAILED(o.As(&o1))) continue;
      ComPtr<IDXGIOutputDuplication> dp;
      HRESULT dr = o1->DuplicateOutput(d.Get(), &dp);
      wprintf(L"      output %u '%s' attached=%d  DuplicateOutput=0x%08lX %s\n", oi, od.DeviceName,
              od.AttachedToDesktop, (unsigned long)dr, SUCCEEDED(dr) ? L"OK" : L"");
      if (SUCCEEDED(dr) && !dupl) {
        dev = d; ctx = c; out1 = o1; dupl = dp; adapter = a; chosen = ad; chosenOut = (int)oi;
      }
    }
  }
  if (!dupl) { printf("\nNo adapter/output could be duplicated. Cannot measure.\n"); return 1; }
  wprintf(L"\n=== CAPTURING on [%s] output %d ===\n", chosen.Description, chosenOut);

  ComPtr<ID3D11DeviceContext1> ctx1; ctx.As(&ctx1);
  ComPtr<IDXGIDevice> dxgiDev; dev.As(&dxgiDev);
  ComPtr<IDXGIFactory2> factory2; HR(factory1.As(&factory2));

  WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = L"M0ProbeB"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
  HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"M0 Probe B - native DDA",
    WS_POPUP, 0, 0, W, H, nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(hwnd, SW_SHOW);

  DXGI_SWAP_CHAIN_DESC1 scd{};
  scd.Width = W; scd.Height = H; scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  scd.SampleDesc.Count = 1; scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  scd.BufferCount = 2; scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  ComPtr<IDXGISwapChain1> swap;
  HR(factory2->CreateSwapChainForHwnd(dev.Get(), hwnd, &scd, nullptr, nullptr, &swap));
  factory2->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
  ComPtr<ID3D11Texture2D> back; HR(swap->GetBuffer(0, IID_PPV_ARGS(&back)));
  ComPtr<ID3D11RenderTargetView> rtv; HR(dev->CreateRenderTargetView(back.Get(), nullptr, &rtv));

  DXGI_OUTDUPL_DESC dd{}; dupl->GetDesc(&dd);
  const int CW = dd.ModeDesc.Width, CH = dd.ModeDesc.Height;

  // owned copy of the desktop image (the DDA texture is not reliably SRV-bindable)
  D3D11_TEXTURE2D_DESC td{};
  td.Width = CW; td.Height = CH; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  ComPtr<ID3D11Texture2D> shot; HR(dev->CreateTexture2D(&td, nullptr, &shot));
  ComPtr<ID3D11ShaderResourceView> srv; HR(dev->CreateShaderResourceView(shot.Get(), nullptr, &srv));

  D3D11_TEXTURE2D_DESC sd = td;                       // 1-row staging strip for the counter
  sd.Width = NBITS * BS; sd.Height = 1; sd.Usage = D3D11_USAGE_STAGING;
  sd.BindFlags = 0; sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> stage; HR(dev->CreateTexture2D(&sd, nullptr, &stage));

  ComPtr<ID3DBlob> vsb, psb, err;
  if (FAILED(D3DCompile(kHLSL, strlen(kHLSL), nullptr, nullptr, nullptr, "VS", "vs_5_0", 0, 0, &vsb, &err)) ||
      FAILED(D3DCompile(kHLSL, strlen(kHLSL), nullptr, nullptr, nullptr, "PS", "ps_5_0", 0, 0, &psb, &err))) {
    printf("shader compile failed: %s\n", err ? (char*)err->GetBufferPointer() : "?"); return 1;
  }
  ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
  HR(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs));
  HR(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps));
  D3D11_SAMPLER_DESC smpd{}; smpd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  smpd.AddressU = smpd.AddressV = smpd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  ComPtr<ID3D11SamplerState> smp; HR(dev->CreateSamplerState(&smpd, &smp));

  D3D11_VIEWPORT vp{ 0, 0, (float)W, (float)H, 0, 1 };
  LARGE_INTEGER qf, t0, tStart, now;
  QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&t0); tStart = t0;

  int counter = 0, frames = 0, newFrames = 0, timeouts = 0, bad = 0;
  double latSum = 0; int latN = 0, lastLat = -1;
  double totFps = 0; int fpsN = 0;
  printf("capture %dx%d, window %dx%d, running %.0fs\n\n", CW, CH, W, H, RUN_SECONDS);

  MSG msg{};
  while (!g_quit) {
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }

    DXGI_OUTDUPL_FRAME_INFO fi{}; ComPtr<IDXGIResource> res;
    HRESULT hr = dupl->AcquireNextFrame(0, &fi, &res);
    if (hr == DXGI_ERROR_WAIT_TIMEOUT) { timeouts++; }
    else if (hr == DXGI_ERROR_ACCESS_LOST) {
      dupl.Reset(); if (FAILED(out1->DuplicateOutput(dev.Get(), &dupl))) { Sleep(50); continue; }
    } else if (SUCCEEDED(hr)) {
      ComPtr<ID3D11Texture2D> desk; res.As(&desk);
      ctx->CopyResource(shot.Get(), desk.Get());
      newFrames++;
      dupl->ReleaseFrame();
    }

    if (frames % 10 == 0) {                            // decode the DELAYED counter
      D3D11_BOX box{ 0, (UINT)(BS/2), 0, (UINT)(NBITS*BS), (UINT)(BS/2 + 1), 1 };
      ctx->CopySubresourceRegion(stage.Get(), 0, 0, 0, 0, shot.Get(), 0, &box);
      D3D11_MAPPED_SUBRESOURCE ms{};
      if (SUCCEEDED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &ms))) {
        const unsigned char* p = (const unsigned char*)ms.pData;
        int v = 0; bool clean = true;
        for (int i = 0; i < NBITS; i++) {
          int s = p[(i*BS + BS/2) * 4];
          if (s > 60 && s < 195) clean = false;
          if (s >= 128) v |= (1 << i);
        }
        ctx->Unmap(stage.Get(), 0);
        if (clean) { int d = (counter - v) & 0xFF; if (d < 40) { lastLat = d; latSum += d; latN++; } else bad++; }
        else bad++;
      }
    }

    ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
    ctx->RSSetViewports(1, &vp);
    ctx->VSSetShader(vs.Get(), nullptr, 0); ctx->PSSetShader(ps.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, srv.GetAddressOf());
    ctx->PSSetSamplers(0, 1, smp.GetAddressOf());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->Draw(3, 0);

    counter = (counter + 1) & 0xFF;                    // draw current counter on top
    if (ctx1) for (int i = 0; i < NBITS; i++) {
      float v = ((counter >> i) & 1) ? 1.f : 0.f; float col[4] = { v, v, v, 1 };
      D3D11_RECT r{ i*BS, 0, i*BS + BS, BS };
      ctx1->ClearView(rtv.Get(), col, &r, 1);
    }
    swap->Present(SYNC, 0);            // SYNC=1 vsync (fair latency), 0 uncapped (fps ceiling)
    frames++;

    QueryPerformanceCounter(&now);
    double el = double(now.QuadPart - t0.QuadPart) / qf.QuadPart;
    if (el >= 1.0) {
      double fps = frames / el; totFps += fps; fpsN++;
      wchar_t t[512];
      swprintf(t, 512, L"present %.1f fps | new frames %.1f/s | timeouts %d | LATENCY %d frames "
                       L"~%.1f ms (avg %.2f f, n=%d) | bad %d",
               fps, newFrames/el, timeouts, lastLat, lastLat < 0 ? -1.0 : lastLat * 1000.0 / fps,
               latN ? latSum/latN : -1.0, latN, bad);
      SetWindowTextW(hwnd, t);
      wprintf(L"%s\n", t); fflush(stdout);
      frames = newFrames = timeouts = 0; QueryPerformanceCounter(&t0);
    }
    if (double(now.QuadPart - tStart.QuadPart) / qf.QuadPart >= RUN_SECONDS) g_quit = true;
  }

  double avgFps = fpsN ? totFps / fpsN : 0, avgLat = latN ? latSum / latN : -1;
  printf("\n=== PROBE B SUMMARY (native DDA) ===\n");
  wprintf(L"adapter        %s\n", chosen.Description);
  printf("capture res    %dx%d\n", CW, CH);
  printf("present fps    %.1f\n", avgFps);
  printf("LATENCY        %.2f frames  ~%.1f ms\n", avgLat, avgLat < 0 ? -1.0 : avgLat * 1000.0 / avgFps);
  printf("samples        n=%d, bad=%d\n", latN, bad);
  return 0;
}
