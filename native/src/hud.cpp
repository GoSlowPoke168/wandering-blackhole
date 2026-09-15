#include "hud.h"
#include <cmath>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

bool Hud::init(ID3D11Device* dev, IDXGISwapChain1* swap, float scale, std::string* err) {
  scale_ = scale;
  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory_));
  if (FAILED(hr)) { *err = "D2D1CreateFactory"; return false; }
  ComPtr<IDXGIDevice> dxgi; dev->QueryInterface(IID_PPV_ARGS(&dxgi));
  ComPtr<ID2D1Device> d2dDev;
  if (FAILED(factory_->CreateDevice(dxgi.Get(), &d2dDev)) ||
      FAILED(d2dDev->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &ctx_))) { *err = "D2D device"; return false; }
  ComPtr<IDXGISurface> surface;
  if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&surface)))) { *err = "swapchain surface"; return false; }
  const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
  if (FAILED(ctx_->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &target_))) { *err = "D2D target"; return false; }
  ctx_->SetTarget(target_.Get());
  ctx_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.72f), &bg_);
  ctx_->CreateSolidColorBrush(D2D1::ColorF(1.f, 176 / 255.f, 0, 0.5f), &border_);
  ctx_->CreateSolidColorBrush(D2D1::ColorF(1.f, 176 / 255.f, 0, 1.f), &fg_);

  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dwrite_.GetAddressOf())))
    { *err = "DWriteCreateFactory"; return false; }
  if (FAILED(dwrite_->CreateTextFormat(L"Consolas", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, 12 * scale, L"en-us", &format_))) { *err = "text format"; return false; }
  format_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, 18 * scale, 14 * scale);   // 12px/1.5, like the CSS
  return true;
}

void Hud::setText(const std::wstring& text) {
  if (text == text_ && layout_) return;
  text_ = text;
  layout_.Reset();
  dwrite_->CreateTextLayout(text_.c_str(), (UINT32)text_.size(), format_.Get(), 800 * scale_, 2000 * scale_, &layout_);
  DWRITE_TEXT_METRICS m{}; if (layout_) layout_->GetMetrics(&m);
  const float padX = 12 * scale_, padY = 8 * scale_, left = 16 * scale_, top = 16 * scale_;
  rect_ = { (LONG)left, (LONG)top, (LONG)std::ceil(left + m.widthIncludingTrailingWhitespace + padX * 2 + 2),
            (LONG)std::ceil(top + m.height + padY * 2 + 2) };
}

void Hud::draw() {
  if (!layout_) return;
  const D2D1_RECT_F box = D2D1::RectF((float)rect_.left, (float)rect_.top, (float)rect_.right - 1, (float)rect_.bottom - 1);
  ctx_->BeginDraw();
  ctx_->FillRoundedRectangle(D2D1::RoundedRect(box, 3 * scale_, 3 * scale_), bg_.Get());
  ctx_->DrawRoundedRectangle(D2D1::RoundedRect(box, 3 * scale_, 3 * scale_), border_.Get(), 1.f);
  ctx_->DrawTextLayout(D2D1::Point2F(box.left + 12 * scale_, box.top + 8 * scale_), layout_.Get(), fg_.Get(),
                       D2D1_DRAW_TEXT_OPTIONS_NONE);
  ctx_->EndDraw();
}
