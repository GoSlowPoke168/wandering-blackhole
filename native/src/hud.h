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
#include <algorithm>
#include <string>

using Microsoft::WRL::ComPtr;

// The on-screen readout, in the terminal this shader came from: amber text against a rule,
// a cursor still blinking under it, and the near-black ground a terminal actually has. An
// outline on the glyphs was tried first and was not enough over real windows.
//
// Text arrives as lines of "key\tvalue". The two columns are laid out separately so each
// draws in one colour, which keeps every pass single-brush.
class Hud {
public:
  bool init(ID3D11Device* dev, IDXGISwapChain1* swap, float scale, std::string* err);
  void setText(const std::wstring& text);
  // 0 leaves the text on the bare desktop, 1 hides everything behind it.
  void setOpacity(float a) { if (scrim_) scrim_->SetOpacity(std::max(0.f, std::min(1.f, a))); }
  RECT rect() const { return rect_; }        // window px, for the dirty-rect union
  void draw();
private:
  ComPtr<ID2D1Factory1> factory_;
  ComPtr<ID2D1DeviceContext> ctx_;
  ComPtr<ID2D1Bitmap1> target_;
  ComPtr<ID2D1SolidColorBrush> fg_, dim_, scrim_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteTextFormat> format_;
  ComPtr<IDWriteTextLayout> keys_, vals_;
  std::wstring text_;
  float scale_ = 1;
  float colX_ = 0;        // x of the value column, relative to the text origin
  float lineH_ = 0;       // one line, for placing the cursor
  float textH_ = 0;       // the laid-out block, without the cursor line
  int lines_ = 0;
  RECT rect_{};
};
