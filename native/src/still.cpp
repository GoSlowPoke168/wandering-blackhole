// Offline render for the shader-port check: --still <in.png> <case.txt> <out.raw> [out.png]
// Renders blackhole.hlsl over a PNG with the uniforms from the case file and writes the
// result as raw RGBA (8-byte w/h header, top-down) plus an optional PNG. tools/still
// renders the same case through WebGL and diffs the two.
#include "renderer.h"
#include <wincodec.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "d3d11.lib")

static std::wstring exeDir() {
  wchar_t p[MAX_PATH]; GetModuleFileNameW(nullptr, p, MAX_PATH);
  std::wstring s(p); return s.substr(0, s.find_last_of(L"\\/"));
}

// "key value" per line; keys are the cbuffer field names, uCenter takes two values.
static bool loadCase(const wchar_t* path, Uniforms& u) {
  std::ifstream f(path);
  if (!f) return false;
  std::string key;
  while (f >> key) {
    if (key[0] == '#') { std::string rest; std::getline(f, rest); continue; }
    if (key == "uCenter") { f >> u.uCenter[0] >> u.uCenter[1]; continue; }
    if (key == "uDesktopXform") { f >> u.uDesktopXform[0] >> u.uDesktopXform[1] >> u.uDesktopXform[2] >> u.uDesktopXform[3]; continue; }
    float v; f >> v;
    if      (key == "iTime")       u.iTime = v;
    else if (key == "TOKEN_LEVEL") u.TOKEN_LEVEL = v;
    else if (key == "uCenterPin")  u.uCenterPin = v;
    else if (key == "uDriftTime")  u.uDriftTime = v;
    else {
      bool hit = false;
      for (int i = 0; i < LOOK_COUNT; i++) if (key == kLookNames[i]) { u.look[i] = v; hit = true; }
      if (!hit) fprintf(stderr, "case: unknown key %s\n", key.c_str());
    }
  }
  return true;
}

int runStill(const wchar_t* pngIn, const wchar_t* caseFile, const wchar_t* rawOut, const wchar_t* pngOut) {
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  ComPtr<IWICImagingFactory> wic;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) { printf("WIC init failed\n"); return 1; }

  // ---- decode the input as BGRA ----
  ComPtr<IWICBitmapDecoder> dec; ComPtr<IWICBitmapFrameDecode> frame; ComPtr<IWICFormatConverter> conv;
  if (FAILED(wic->CreateDecoderFromFilename(pngIn, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec)) ||
      FAILED(dec->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
      FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) {
    printf("cannot decode %ls\n", pngIn); return 1;
  }
  UINT w = 0, h = 0; conv->GetSize(&w, &h);
  std::vector<unsigned char> src((size_t)w * h * 4);
  conv->CopyPixels(nullptr, w * 4, (UINT)src.size(), src.data());

  // ---- device + textures ----
  ComPtr<ID3D11Device> dev; ComPtr<ID3D11DeviceContext> ctx0; D3D_FEATURE_LEVEL fl;
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                               nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx0))) { printf("D3D11CreateDevice failed\n"); return 1; }
  ComPtr<ID3D11DeviceContext1> ctx; ctx0.As(&ctx);

  D3D11_TEXTURE2D_DESC td{};
  td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
  td.Format = DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count = 1;
  td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA init{ src.data(), w * 4, 0 };
  ComPtr<ID3D11Texture2D> desk; dev->CreateTexture2D(&td, &init, &desk);
  ComPtr<ID3D11ShaderResourceView> srv; dev->CreateShaderResourceView(desk.Get(), nullptr, &srv);

  td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
  ComPtr<ID3D11Texture2D> target; dev->CreateTexture2D(&td, nullptr, &target);
  ComPtr<ID3D11RenderTargetView> rtv; dev->CreateRenderTargetView(target.Get(), nullptr, &rtv);
  td.Usage = D3D11_USAGE_STAGING; td.BindFlags = 0; td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging; dev->CreateTexture2D(&td, nullptr, &staging);

  // ---- render ----
  Renderer r; std::string err;
  if (!r.init(dev.Get(), exeDir() + L"\\shader\\blackhole.hlsl", &err)) { printf("shader: %s\n", err.c_str()); return 1; }
  Uniforms u; defaultUniforms(u);
  u.iResolution[0] = (float)w; u.iResolution[1] = (float)h;
  if (!loadCase(caseFile, u)) { printf("cannot read case %ls\n", caseFile); return 1; }
  const float clear[4] = { 0, 0, 0, 0 };
  ctx->ClearRenderTargetView(rtv.Get(), clear);
  r.draw(ctx.Get(), rtv.Get(), srv.Get(), u, (int)w, (int)h, nullptr);
  ctx->CopyResource(staging.Get(), target.Get());

  D3D11_MAPPED_SUBRESOURCE ms{};
  if (FAILED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &ms))) { printf("Map failed\n"); return 1; }
  std::vector<unsigned char> rgba((size_t)w * h * 4), bgra((size_t)w * h * 4);
  for (UINT y = 0; y < h; y++) {
    const unsigned char* row = (const unsigned char*)ms.pData + y * ms.RowPitch;
    memcpy(&bgra[(size_t)y * w * 4], row, (size_t)w * 4);
    for (UINT x = 0; x < w; x++) {
      const unsigned char* p = row + x * 4; unsigned char* o = &rgba[((size_t)y * w + x) * 4];
      o[0] = p[2]; o[1] = p[1]; o[2] = p[0]; o[3] = p[3];
    }
  }
  ctx->Unmap(staging.Get(), 0);

  // ---- write raw ----
  std::ofstream out(rawOut, std::ios::binary);
  unsigned int hdr[2] = { w, h };
  out.write((const char*)hdr, 8); out.write((const char*)rgba.data(), (std::streamsize)rgba.size());
  out.close();

  // ---- optional PNG for eyeballing ----
  if (pngOut) {
    ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> enc; ComPtr<IWICBitmapFrameEncode> fe;
    wic->CreateStream(&stream); stream->InitializeFromFilename(pngOut, GENERIC_WRITE);
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc); enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    enc->CreateNewFrame(&fe, nullptr); fe->Initialize(nullptr);
    fe->SetSize(w, h); WICPixelFormatGUID pf = GUID_WICPixelFormat32bppBGRA; fe->SetPixelFormat(&pf);
    fe->WritePixels(h, w * 4, (UINT)bgra.size(), bgra.data());
    fe->Commit(); enc->Commit();
  }
  printf("still: %ux%u -> %ls\n", w, h, rawOut);
  return 0;
}
