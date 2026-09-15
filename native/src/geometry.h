#pragma once
// Port of src/geometry.js. All monitors are one continuous canvas: the hole has a single
// position in "virtual uv" (0..1 across the bounding box of every display) and each
// overlay converts that into its own window uv. Physical pixels throughout.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <vector>

struct UV { float x, y; };

inline RECT virtualBounds(const std::vector<RECT>& displays) {
  if (displays.empty()) return RECT{ 0, 0, 1, 1 };
  RECT r = displays[0];
  for (const RECT& d : displays) {
    r.left = std::min(r.left, d.left);   r.top = std::min(r.top, d.top);
    r.right = std::max(r.right, d.right); r.bottom = std::max(r.bottom, d.bottom);
  }
  if (r.right <= r.left) r.right = r.left + 1;
  if (r.bottom <= r.top) r.bottom = r.top + 1;
  return r;
}

inline UV toWindowUV(UV hole, const RECT& win, const RECT& virt) {
  const float vw = (float)(virt.right - virt.left), vh = (float)(virt.bottom - virt.top);
  const float px = virt.left + hole.x * vw, py = virt.top + hole.y * vh;
  return { (px - win.left) / std::max(1L, win.right - win.left),
           (py - win.top)  / std::max(1L, win.bottom - win.top) };
}

// Is the hole close enough to this window to be worth shading? Generous margin: the
// bright disk reaches roughly 3x the shadow radius.
inline bool shouldShade(UV winUV, float level) {
  const float margin = 0.35f + 0.9f * std::max(0.f, std::min(1.f, level));
  return winUV.x > -margin && winUV.x < 1 + margin && winUV.y > -margin && winUV.y < 1 + margin;
}

// Lissajous wander over the virtual desktop; incommensurate frequencies so it never
// visibly repeats, inset from the edges by an amount that grows with the hole.
inline UV wanderUV(double t, float level, float aspect) {
  const float lvl = std::max(0.f, std::min(1.f, level));
  const float inset = 0.06f + 0.22f * lvl;
  const float lo = inset, span = std::max(0.02f, 1 - inset * 2);
  const float yLo = inset, ySpan = std::max(0.02f, (1 - inset - 0.20f) - inset);
  const float fx = 0.055f * std::max(1.f, std::min(2.5f, aspect / 1.6f));
  const float fy = 0.043f;
  return { lo  + span  * (0.5f + 0.5f * (float)std::sin(t * fx)),
           yLo + ySpan * (0.5f + 0.5f * (float)std::sin(t * fy + 1.37)) };
}
