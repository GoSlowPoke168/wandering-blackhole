'use strict';
// Generates the tray icon at runtime - a shadow with a photon ring - so the repo
// carries no binary asset and the icon can reflect state (it dims when idle).
const zlib = require('zlib');

const CRC = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c;
  }
  return buf => {
    let c = -1;
    for (let i = 0; i < buf.length; i++) c = t[(c ^ buf[i]) & 0xFF] ^ (c >>> 8);
    return (c ^ -1) >>> 0;
  };
})();

function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(CRC(body));
  return Buffer.concat([len, body, crc]);
}

function png(w, h, rgba) {
  const sig = Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8;    // bit depth
  ihdr[9] = 6;    // colour type: RGBA
  // raw scanlines, each prefixed with filter byte 0
  const raw = Buffer.alloc(h * (w * 4 + 1));
  for (let y = 0; y < h; y++) {
    raw[y * (w * 4 + 1)] = 0;
    rgba.copy(raw, y * (w * 4 + 1) + 1, y * w * 4, (y + 1) * w * 4);
  }
  return Buffer.concat([sig, chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

// dim: 0..1 overall brightness, so the tray can show "idle / paused" at a glance.
function trayIcon(size = 32, dim = 1) {
  const px = Buffer.alloc(size * size * 4);
  const c = (size - 1) / 2;
  const R_SHADOW = size * 0.22, R_RING = size * 0.30, R_DISK = size * 0.46;
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const d = Math.hypot(x - c, y - c);
      let r = 0, g = 0, b = 0, a = 0;
      if (d <= R_SHADOW + 0.5) {
        a = Math.min(1, R_SHADOW + 0.5 - d);            // the shadow: opaque black
      } else if (d <= R_DISK) {
        // photon ring peaks just outside the shadow, then falls off through the disk
        const ring = Math.exp(-Math.pow((d - R_RING) / (size * 0.055), 2));
        const tail = Math.max(0, 1 - (d - R_RING) / (R_DISK - R_RING));
        const i = Math.min(1, ring + tail * 0.45);
        r = 255 * i; g = 150 * i; b = 30 * i;
        a = Math.min(1, i * 1.3);
      }
      const o = (y * size + x) * 4;
      px[o]     = Math.round(r * dim);
      px[o + 1] = Math.round(g * dim);
      px[o + 2] = Math.round(b * dim);
      px[o + 3] = Math.round(a * 255);
    }
  }
  return png(size, size, px);
}

module.exports = { trayIcon };
