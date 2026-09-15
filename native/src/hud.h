#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

// The on-screen HUD: Direct2D text drawn straight onto the swapchain back buffer after
// the shader. Nothing is created until the HUD is first shown, and drawing it costs a
// small text layout inside the dirty rect - zero when it is off.
class Hud {
public:
  bool init(ID3D11Device* dev, IDXGISwapChain1* swap, float scale, std::string* err);
  void setText(const std::wstring& text);
  RECT rect() const { return rect_; }        // window px, for the dirty-rect union
  void draw();
private:
  ComPtr<ID2D1Factory1> factory_;
  ComPtr<ID2D1DeviceContext> ctx_;
  ComPtr<ID2D1Bitmap1> target_;
  ComPtr<ID2D1SolidColorBrush> bg_, border_, fg_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteTextFormat> format_;
  ComPtr<IDWriteTextLayout> layout_;
  std::wstring text_;
  float scale_ = 1;
  RECT rect_{};
};
