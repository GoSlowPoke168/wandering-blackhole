#pragma once
#include <windows.h>
#include <d3d11_1.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

// The 26 tunables promoted from const to uniform, in the shader-patch PROMOTE order.
// Order matters: it is the cbuffer layout.
enum Look {
  DISK_TEMP, DISK_INCL, DISK_ROLL, DISK_INNER, DISK_OUTER, DISK_OPACITY, DOPPLER_MIX,
  DISK_BEAM, DISK_GAIN, DISK_CONTRAST, DISK_WIND, DISK_SPEED, EXPOSURE, STAR_GAIN,
  HOLE_RADIUS, LENS_DEPTH, WORK_AREA, DILATION_MIN, TOKEN_AREA_MIN, TOKEN_AREA_MAX,
  TOKEN_EASE, TOKEN_REACH, TOKEN_CALM, TOKEN_RUSH, TOKEN_HOME_X, TOKEN_HOME_Y,
  LOOK_COUNT
};
extern const char* const kLookNames[LOOK_COUNT];

// Mirror of `cbuffer Uniforms` in shader/blackhole.hlsl. Keep the two in step.
struct alignas(16) Uniforms {
  float iResolution[2]; float iTime;      float TOKEN_LEVEL;
  float uDesktopXform[4];
  float uCenter[2];     float uCenterPin; float uDriftTime;
  float look[LOOK_COUNT];
  float pad[2];
};
static_assert(sizeof(Uniforms) == 160, "cbuffer layout drifted from blackhole.hlsl");

// Compiles the shader once and draws a full-screen triangle with it. Owns no target and
// no capture: the overlay and the offline still renderer both drive it.
class Renderer {
public:
  bool init(ID3D11Device* dev, const std::wstring& hlslPath, std::string* err);
  void draw(ID3D11DeviceContext1* ctx, ID3D11RenderTargetView* rtv, ID3D11ShaderResourceView* desktop,
            const Uniforms& u, int width, int height, const D3D11_RECT* scissor);
private:
  ComPtr<ID3D11VertexShader> vs_;
  ComPtr<ID3D11PixelShader> ps_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11RasterizerState> raster_;
  ComPtr<ID3D11Buffer> cbuf_;
};

// Default look values, harvested from the shader by tools/gen-presets.js.
void defaultUniforms(Uniforms& u);
