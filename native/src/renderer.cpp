#include "renderer.h"
#include "presets.gen.h"
#include <d3dcompiler.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "d3dcompiler.lib")

const char* const kLookNames[LOOK_COUNT] = {
  "DISK_TEMP", "DISK_INCL", "DISK_ROLL", "DISK_INNER", "DISK_OUTER", "DISK_OPACITY", "DOPPLER_MIX",
  "DISK_BEAM", "DISK_GAIN", "DISK_CONTRAST", "DISK_WIND", "DISK_SPEED", "EXPOSURE", "STAR_GAIN",
  "HOLE_RADIUS", "LENS_DEPTH", "WORK_AREA", "DILATION_MIN", "TOKEN_AREA_MIN", "TOKEN_AREA_MAX",
  "TOKEN_EASE", "TOKEN_REACH", "TOKEN_CALM", "TOKEN_RUSH", "TOKEN_HOME_X", "TOKEN_HOME_Y",
};

void defaultUniforms(Uniforms& u) {
  u = Uniforms{};
  u.uDesktopXform[0] = u.uDesktopXform[1] = 1.f;
  u.uCenter[0] = 0.5f; u.uCenter[1] = 0.35f;
  u.uCenterPin = 1.f;
  u.TOKEN_LEVEL = 0.25f;
  for (int i = 0; i < LOOK_COUNT; i++) u.look[i] = kLookDefaults[i];
}

bool Renderer::init(ID3D11Device* dev, const std::wstring& hlslPath, std::string* err) {
  std::ifstream f(hlslPath, std::ios::binary);
  if (!f) { *err = "cannot open shader file"; return false; }
  std::stringstream ss; ss << f.rdbuf();
  const std::string src = ss.str();

  ComPtr<ID3DBlob> vsb, psb, e;
  const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
  if (FAILED(D3DCompile(src.data(), src.size(), "blackhole.hlsl", nullptr, nullptr, "VS", "vs_5_0", flags, 0, &vsb, &e)) ||
      FAILED(D3DCompile(src.data(), src.size(), "blackhole.hlsl", nullptr, nullptr, "PS", "ps_5_0", flags, 0, &psb, &e))) {
    *err = e ? std::string((const char*)e->GetBufferPointer(), e->GetBufferSize()) : "D3DCompile failed";
    return false;
  }
  if (FAILED(dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs_)) ||
      FAILED(dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps_))) {
    *err = "shader object creation failed"; return false;
  }
  D3D11_SAMPLER_DESC sd{}; sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
  sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
  dev->CreateSamplerState(&sd, &sampler_);
  D3D11_RASTERIZER_DESC rd{}; rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE;
  rd.ScissorEnable = TRUE; rd.DepthClipEnable = TRUE;
  dev->CreateRasterizerState(&rd, &raster_);
  D3D11_BUFFER_DESC bd{}; bd.ByteWidth = sizeof(Uniforms); bd.Usage = D3D11_USAGE_DEFAULT;
  bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  dev->CreateBuffer(&bd, nullptr, &cbuf_);
  return true;
}

void Renderer::draw(ID3D11DeviceContext1* ctx, ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* desktop,
                    const Uniforms& u, int width, int height, const D3D11_RECT* scissor) {
  D3D11_VIEWPORT vp{ 0, 0, (float)width, (float)height, 0, 1 };
  D3D11_RECT full{ 0, 0, width, height };
  ctx->OMSetRenderTargets(1, &rtv, nullptr);
  ctx->RSSetViewports(1, &vp);
  ctx->RSSetScissorRects(1, scissor ? scissor : &full);
  ctx->RSSetState(raster_.Get());
  ctx->UpdateSubresource(cbuf_.Get(), 0, nullptr, &u, 0, 0);
  ctx->VSSetShader(vs_.Get(), nullptr, 0);
  ctx->PSSetShader(ps_.Get(), nullptr, 0);
  ctx->PSSetConstantBuffers(0, 1, cbuf_.GetAddressOf());
  ctx->PSSetShaderResources(0, 1, &desktop);
  ctx->PSSetSamplers(0, 1, sampler_.GetAddressOf());
  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->Draw(3, 0);
}
