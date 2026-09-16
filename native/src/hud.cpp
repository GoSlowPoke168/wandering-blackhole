#include "hud.h"
#include <algorithm>
#include <cmath>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

// Geometry, in unscaled px. The rule is the only chrome there is.
static const float kOriginX = 18, kOriginY = 16, kBarW = 3, kBarGap = 11;
static const float kFontSize = 13, kLineH = 22.75f, kColGap = 2;   // colGap in character widths

bool Hud::init(ID3D11Device* dev, IDXGISwapChain1* swap, float scale, std::string* err) {
  scale_ = scale;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&factory_)))) { *err = "D2D1CreateFactory"; return false; }
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

  ctx_->CreateSolidColorBrush(D2D1::ColorF(1.f, 176 / 255.f, 0.f, 1.f), &fg_);        // #FFB000
  ctx_->CreateSolidColorBrush(D2D1::ColorF(0x9A / 255.f, 0x73 / 255.f, 0x30 / 255.f, 1.f), &dim_);
  ctx_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.85f), &shadow_);

  if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)dwrite_.GetAddressOf())))
    { *err = "DWriteCreateFactory"; return false; }
  // Cascadia is the terminal face this project's shader grew up in; Consolas ships
  // everywhere and is the honest fallback rather than letting DirectWrite pick silently.
  const wchar_t* family = L"Consolas";
  ComPtr<IDWriteFontCollection> fonts;
  if (SUCCEEDED(dwrite_->GetSystemFontCollection(&fonts)) && fonts) {
    UINT32 idx = 0; BOOL found = FALSE;
    if (SUCCEEDED(fonts->FindFamilyName(L"Cascadia Mono", &idx, &found)) && found) family = L"Cascadia Mono";
  }
  if (FAILED(dwrite_->CreateTextFormat(family, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, kFontSize * scale_, L"en-us", &format_)))
    { *err = "text format"; return false; }
  format_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, kLineH * scale_, kFontSize * 1.05f * scale_);
  return true;
}

void Hud::setText(const std::wstring& text) {
  if (text == text_ && keys_) return;
  text_ = text;

  // Split "key\tvalue" lines into two columns.
  std::wstring keys, vals;
  size_t pos = 0;
  lines_ = 0;
  while (pos <= text.size()) {
    const size_t nl = text.find(L'\n', pos);
    const std::wstring line = text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos);
    const size_t tab = line.find(L'\t');
    if (lines_) { keys += L'\n'; vals += L'\n'; }
    keys += tab == std::wstring::npos ? line : line.substr(0, tab);
    vals += tab == std::wstring::npos ? L"" : line.substr(tab + 1);
    lines_++;
    if (nl == std::wstring::npos) break;
    pos = nl + 1;
  }

  keys_.Reset(); vals_.Reset();
  const float wide = 4000 * scale_;
  dwrite_->CreateTextLayout(keys.c_str(), (UINT32)keys.size(), format_.Get(), wide, wide, &keys_);
  dwrite_->CreateTextLayout(vals.c_str(), (UINT32)vals.size(), format_.Get(), wide, wide, &vals_);
  if (!keys_ || !vals_) return;

  // Monospace, so one character width sets the gutter between the columns.
  DWRITE_TEXT_METRICS km{}, vm{};
  keys_->GetMetrics(&km); vals_->GetMetrics(&vm);
  ComPtr<IDWriteTextLayout> em;
  float chw = kFontSize * 0.6f * scale_;
  if (SUCCEEDED(dwrite_->CreateTextLayout(L"MMMMMMMMMM", 10, format_.Get(), wide, wide, &em)) && em) {
    DWRITE_TEXT_METRICS m{}; em->GetMetrics(&m); chw = m.widthIncludingTrailingWhitespace / 10.f;
  }
  colX_ = km.widthIncludingTrailingWhitespace + chw * kColGap;
  lineH_ = kLineH * scale_;

  const float textX = kOriginX * scale_ + kBarW * scale_ + kBarGap * scale_;
  const float w = colX_ + vm.widthIncludingTrailingWhitespace;
  const float h = std::max(km.height, vm.height);
  rect_ = { (LONG)(kOriginX * scale_), (LONG)(kOriginY * scale_),
            (LONG)std::ceil(textX + w + 4 * scale_),
            (LONG)std::ceil(kOriginY * scale_ + h + lineH_ + 2 * scale_) };   // + the cursor line
}

// A box behind the text would make this a panel again, so the glyphs carry their own
// contrast: a one-pixel outline in near-black, then the fill.
void Hud::drawOutlined(IDWriteTextLayout* layout, float x, float y, ID2D1Brush* fill) {
  const float o = std::max(1.f, std::floor(scale_));
  const D2D1_POINT_2F around[4] = { { x - o, y }, { x + o, y }, { x, y - o }, { x, y + o } };
  for (const auto& p : around) ctx_->DrawTextLayout(p, layout, shadow_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
  ctx_->DrawTextLayout(D2D1::Point2F(x, y), layout, fill, D2D1_DRAW_TEXT_OPTIONS_NONE);
}

void Hud::draw() {
  if (!keys_ || !vals_) return;
  const float x0 = kOriginX * scale_, y0 = kOriginY * scale_;
  const float textX = x0 + kBarW * scale_ + kBarGap * scale_;
  const float h = (float)(rect_.bottom - rect_.top) - lineH_ - 2 * scale_;

  ctx_->BeginDraw();
  ctx_->FillRectangle(D2D1::RectF(x0, y0, x0 + kBarW * scale_, y0 + h), fg_.Get());
  drawOutlined(keys_.Get(), textX, y0, dim_.Get());
  drawOutlined(vals_.Get(), textX + colX_, y0, fg_.Get());
  // A prompt waiting on its own line, blinking at the terminal's own cadence.
  if (GetTickCount64() % 1200 < 620) {
    const float cy = y0 + h + 4 * scale_;
    ctx_->FillRectangle(D2D1::RectF(textX, cy, textX + kFontSize * 0.6f * scale_, cy + kFontSize * scale_), fg_.Get());
  }
  ctx_->EndDraw();
}
