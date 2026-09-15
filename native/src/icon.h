#pragma once
// Port of src/icon.js: the tray icon drawn at runtime - a shadow with a photon ring - so
// there is no binary asset and it can dim to show idle / paused.
#include <windows.h>
#include <cmath>
#include <algorithm>

inline HICON trayIcon(int size, float dim) {
  BITMAPV5HEADER bi{};
  bi.bV5Size = sizeof(bi); bi.bV5Width = size; bi.bV5Height = -size; bi.bV5Planes = 1;
  bi.bV5BitCount = 32; bi.bV5Compression = BI_BITFIELDS;
  bi.bV5RedMask = 0x00FF0000; bi.bV5GreenMask = 0x0000FF00; bi.bV5BlueMask = 0x000000FF; bi.bV5AlphaMask = 0xFF000000;
  void* bits = nullptr;
  HDC dc = GetDC(nullptr);
  HBITMAP color = CreateDIBSection(dc, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, dc);
  if (!color) return nullptr;

  const float c = (size - 1) / 2.f;
  const float R_SHADOW = size * 0.22f, R_RING = size * 0.30f, R_DISK = size * 0.46f;
  unsigned char* px = (unsigned char*)bits;
  for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) {
    const float d = std::hypot(x - c, y - c);
    float r = 0, g = 0, b = 0, a = 0;
    if (d <= R_SHADOW + 0.5f) a = std::min(1.f, R_SHADOW + 0.5f - d);
    else if (d <= R_DISK) {
      const float ring = std::exp(-std::pow((d - R_RING) / (size * 0.055f), 2.f));
      const float tail = std::max(0.f, 1 - (d - R_RING) / (R_DISK - R_RING));
      const float i = std::min(1.f, ring + tail * 0.45f);
      r = 255 * i; g = 150 * i; b = 30 * i; a = std::min(1.f, i * 1.3f);
    }
    unsigned char* o = px + (y * size + x) * 4;      // BGRA, premultiplied as GDI expects
    o[0] = (unsigned char)std::lround(b * dim * a);
    o[1] = (unsigned char)std::lround(g * dim * a);
    o[2] = (unsigned char)std::lround(r * dim * a);
    o[3] = (unsigned char)std::lround(a * 255);
  }
  HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr);
  ICONINFO ii{}; ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
  HICON icon = CreateIconIndirect(&ii);
  DeleteObject(color); DeleteObject(mask);
  return icon;
}
