'use strict';
// Virtual-desktop geometry. All monitors are treated as one continuous canvas, so the
// hole has a single position in "virtual uv" (0..1 across the bounding box of every
// display) and each overlay window converts that into its own canvas uv.
//
// Pure functions of plain rects - no Electron - so the multi-monitor maths can be
// tested on a single-display machine.

// Bounding box of every display, in DIP.
function virtualBounds(displays) {
  let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
  for (const d of displays) {
    const b = d.bounds;
    minX = Math.min(minX, b.x);        minY = Math.min(minY, b.y);
    maxX = Math.max(maxX, b.x + b.width); maxY = Math.max(maxY, b.y + b.height);
  }
  if (!isFinite(minX)) return { x: 0, y: 0, width: 1, height: 1 };
  return { x: minX, y: minY, width: Math.max(1, maxX - minX), height: Math.max(1, maxY - minY) };
}

// Hole position in virtual uv -> uv within one window's canvas. Values outside 0..1
// mean the hole is on another monitor; the shader degrades gracefully there (the
// deflection falls off), and shouldShade() skips those windows entirely.
function toWindowUV(holeUV, winBounds, virt) {
  const px = virt.x + holeUV[0] * virt.width;
  const py = virt.y + holeUV[1] * virt.height;
  return [
    (px - winBounds.x) / Math.max(1, winBounds.width),
    (py - winBounds.y) / Math.max(1, winBounds.height),
  ];
}

// Is the hole close enough to this window to be worth shading? The bright disk reaches
// roughly 3x the shadow radius, so the margin is generous; getting this wrong costs a
// clipped edge, and being generous only costs a little GPU on one extra monitor.
function shouldShade(winUV, level) {
  const margin = 0.35 + 0.9 * Math.max(0, Math.min(1, level));
  return winUV[0] > -margin && winUV[0] < 1 + margin
      && winUV[1] > -margin && winUV[1] < 1 + margin;
}

// Lissajous wander over the virtual desktop. Incommensurate frequencies so the path
// never visibly repeats. Inset from the edges by an amount that grows with the hole so
// a big hole does not spend its time half off-screen.
function wanderUV(t, level, aspect) {
  const lvl = Math.max(0, Math.min(1, level));
  const inset = 0.06 + 0.22 * lvl;
  const lo = inset, span = Math.max(0.02, 1 - inset * 2);
  // Keep clear of the bottom of the screen. The overlay window stops above the taskbar,
  // so a hole drifting down there gets its disk clipped by the window edge - and
  // upstream keeps the hole out of the bottom third anyway, on the grounds that it is
  // where you are working.
  const yLo = inset, ySpan = Math.max(0.02, (1 - inset - 0.20) - inset);
  // Incommensurate frequencies per axis, so the path never repeats. Tuned slow on
  // purpose: this is meant to read as ambient drift, not as something moving. At 1x a
  // crossing takes roughly a minute. x gets a nudge on a wide multi-monitor
  // arrangement so it does not crawl across a very long virtual desktop.
  const fx = 0.055 * Math.max(1, Math.min(2.5, aspect / 1.6));
  const fy = 0.043;
  return [
    lo   + span  * (0.5 + 0.5 * Math.sin(t * fx)),
    yLo  + ySpan * (0.5 + 0.5 * Math.sin(t * fy + 1.37)),
  ];
}

module.exports = { virtualBounds, toWindowUV, shouldShade, wanderUV };
