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

// The on-screen readout, in the terminal this shader came from: no panel, no border, just
// amber text against a rule with a cursor still blinking under it. Legibility over an
// arbitrary desktop comes from outlining the glyphs rather than from a box behind them.
//
// Text arrives as lines of "key\tvalue". The two columns are laid out separately so each
// draws in one colour, which keeps every pass single-brush.
class Hud {
public:
  bool init(ID3D11Device* dev, IDXGISwapChain1* swap, float scale, std::string* err);
  void setText(const std::wstring& text);
  RECT rect() const { return rect_; }        // window px, for the dirty-rect union
  void draw();
private:
  void drawOutlined(IDWriteTextLayout* layout, float x, float y, ID2D1Brush* fill);

  ComPtr<ID2D1Factory1> factory_;
  ComPtr<ID2D1DeviceContext> ctx_;
  ComPtr<ID2D1Bitmap1> target_;
  ComPtr<ID2D1SolidColorBrush> fg_, dim_, shadow_;
  ComPtr<IDWriteFactory> dwrite_;
  ComPtr<IDWriteTextFormat> format_;
  ComPtr<IDWriteTextLayout> keys_, vals_;
  std::wstring text_;
  float scale_ = 1;
  float colX_ = 0;        // x of the value column, relative to the text origin
  float lineH_ = 0;       // one line, for placing the cursor
  int lines_ = 0;
  RECT rect_{};
};
