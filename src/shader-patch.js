'use strict';
// Turns the vendored Ghostty shader into an overlay shader, by text patch at load
// time so shader/blackhole.glsl stays pristine upstream and stays easy to update.
//
// Every patch asserts that it actually applied, so a future upstream update fails
// loudly here rather than silently producing a subtly wrong shader.
const fs = require('fs');
const path = require('path');

const SHADER = path.join(__dirname, '..', 'shader', 'blackhole.glsl');

// Tunables promoted from `const float` to `uniform float` so the host can drive
// them live.
const PROMOTE = [
  // disk look
  'DISK_TEMP', 'DISK_INCL', 'DISK_ROLL', 'DISK_INNER', 'DISK_OUTER', 'DISK_OPACITY',
  'DOPPLER_MIX', 'DISK_BEAM', 'DISK_GAIN', 'DISK_CONTRAST', 'DISK_WIND', 'DISK_SPEED',
  'EXPOSURE', 'STAR_GAIN',
  // hole & lensing
  'HOLE_RADIUS', 'LENS_DEPTH', 'WORK_AREA', 'DILATION_MIN',
  // size / roam mapping
  'TOKEN_AREA_MIN', 'TOKEN_AREA_MAX', 'TOKEN_EASE', 'TOKEN_REACH',
  'TOKEN_CALM', 'TOKEN_RUSH', 'TOKEN_HOME_X', 'TOKEN_HOME_Y',
];

// The 14 fields of DiskLook, in declaration order.
const LOOK_FIELDS = ['DISK_TEMP','DISK_INCL','DISK_ROLL','DISK_INNER','DISK_OUTER',
  'DISK_OPACITY','DOPPLER_MIX','DISK_BEAM','DISK_GAIN','DISK_CONTRAST','DISK_WIND',
  'DISK_SPEED','EXPOSURE','STAR_GAIN'];

function header(promoted) {
  return `#version 300 es
precision highp float;
precision highp int;
uniform sampler2D iChannel0;
uniform vec3  iResolution;
uniform float iTime;
uniform vec4  iDate;
uniform vec4  iCurrentCursorColor;
uniform vec4  iPreviousCursorColor;
uniform float iTimeCursorChange;
uniform float TOKEN_LEVEL;

// The overlay window does NOT necessarily cover the whole display - Windows clamps
// a popup window to the work area, so it typically stops above the taskbar - while
// the capture always covers the FULL screen. Without this remap the shader would
// sample the desktop vertically compressed and the lens would not line up with the
// real desktop showing through around it.
//   xy = canvas->screen scale, zw = canvas->screen offset, both in uv space.
uniform vec4 uDesktopXform;
vec4 _sampleDesktop(vec2 uv){ return texture(iChannel0, uv * uDesktopXform.xy + uDesktopXform.zw); }

// Host-owned drift clock. Upstream derives drift from iTime * DRIFT_SPEED and warns
// in-file that rescaling t teleports the hole, because iTime is large and a changed
// multiplier jumps the phase. So the host integrates its own clock instead
// (driftTime += dt * speed): speed changes stay continuous, and speed 0 freezes.
uniform float uDriftTime;

// Manual position override. uCenterPin 0 = upstream's own roaming, 1 = pinned to
// uCenter, in between eases between the two.
uniform vec2  uCenter;
uniform float uCenterPin;

${promoted.map(n => `uniform float ${n};`).join('\n')}

out vec4 _fragOut;
`;
}

// Ghostty's fragCoord.y runs top-down, opposite the Shadertoy convention the shader
// otherwise follows; WebGL is bottom-up, so flip to match.
const FOOTER = `
void main(){ mainImage(_fragOut, vec2(gl_FragCoord.x, iResolution.y - gl_FragCoord.y)); }
`;

function build() {
  let src = fs.readFileSync(SHADER, 'utf8');
  const applied = [];
  const defaults = {};

  const apply = (name, find, replace, expect) => {
    if (expect !== undefined) {
      const n = (src.match(find) || []).length;
      if (n !== expect) throw new Error(`patch "${name}": expected ${expect} matches, found ${n}`);
    }
    const before = src;
    src = src.replace(find, replace);
    if (src === before) throw new Error(`patch "${name}" did not apply - upstream shader changed?`);
    applied.push(name);
  };

  // 1. Promote tunables to uniforms, recording upstream's value as our default.
  for (const name of PROMOTE) {
    const re = new RegExp(`^const float ${name}(\\s*)= *(-?[0-9.]+);`, 'm');
    const m = src.match(re);
    if (!m) throw new Error(`promote "${name}": declaration not found`);
    defaults[name] = parseFloat(m[2]);
    src = src.replace(re, `// ${name} promoted to uniform (upstream default ${m[2]})`);
  }
  applied.push(`promoted ${PROMOTE.length} tunables to uniforms`);

  // 2. TOKEN_LEVEL is a #define fallback used when the cursor carries no signal.
  apply('TOKEN_LEVEL define -> uniform',
    /^#define\s+TOKEN_LEVEL\s+.*$/m,
    '// TOKEN_LEVEL supplied as a uniform by the host');

  // 3. LOOK_DEFAULT is a const struct built from those tunables - illegal once they
  //    are uniforms, since GLSL globals need constant initialisers. It has exactly
  //    one use, inside a function, so inline it there.
  apply('drop const LOOK_DEFAULT',
    /const DiskLook LOOK_DEFAULT = DiskLook\([\s\S]*?\);\n/,
    '// LOOK_DEFAULT inlined at its single use site (its fields are uniforms now)\n');
  apply('inline LOOK_DEFAULT at use site',
    'DiskLook L = LOOK_DEFAULT;',
    `DiskLook L = DiskLook(${LOOK_FIELDS.join(', ')});`);

  // 4. Host owns the drift clock (see uDriftTime above).
  apply('host-owned drift clock',
    /float t = iTime \* DRIFT_SPEED;/,
    'float t = uDriftTime;   // host-owned: continuous speed changes, 0 = frozen');

  // 5. Manual position override, applied after whichever mode computed `center`
  //    and before the first use of it.
  apply('manual position override',
    'float vis = smoothstep(0.0, 0.10, I);',
    'center = mix(center, uCenter, clamp(uCenterPin, 0.0, 1.0));\n' +
    '    float vis = smoothstep(0.0, 0.10, I);');

  // 6. Route all desktop sampling through the uv remap.
  apply('desktop sampling -> remapped helper', /texture\(iChannel0,/g, '_sampleDesktop(', 4);

  // 7. Ghostty composites over the terminal it already drew, so it can emit the
  //    background verbatim where nothing is happening. An overlay cannot: that would
  //    paint a 1-2 frame stale copy over the live desktop and ghost whenever anything
  //    moves. Those paths become fully transparent instead.
  apply('passthrough paths -> transparent', /fragColor = _sampleDesktop\(\s*uv\);/g,
    'fragColor = vec4(0.0);', 2);

  // 8. The weak-field region covers most of the screen with a sub-pixel bend. Drawn
  //    opaque it would ghost a stale desktop everywhere, so fade alpha in with the
  //    deflection: invisible below ~a quarter pixel, solid by ~1.2 px. At the handoff
  //    circle the deflection is largest, so alpha is already ~1 and meets the
  //    geodesic region seamlessly.
  apply('weak-field alpha from deflection',
    'fragColor = vec4(term + stars(d) * L.star * window * shield, 1.0);',
    'vec3 _rgb = term + stars(d) * L.star * window * shield;\n' +
    '        float _a = clamp(smoothstep(0.25, 1.2, defl * iResolution.y)\n' +
    '                       + L.star * window * shield * 0.5, 0.0, 1.0);\n' +
    '        fragColor = vec4(_rgb * _a, _a);   // premultiplied');

  return { source: header(PROMOTE) + src + FOOTER, applied, defaults, promoted: PROMOTE.slice() };
}

// Upstream ships its tuner's presets as literal DiskLook rows in DEMO_TOUR. Harvest
// them rather than inventing our own look library - they are already art-directed,
// and they stay in sync if the shader is updated.
function presets() {
  const src = fs.readFileSync(SHADER, 'utf8');
  const block = src.match(/const DiskLook DEMO_TOUR\[[\s\S]*?\);\n/);
  if (!block) throw new Error('DEMO_TOUR not found - upstream shader changed?');
  const out = [];
  const seen = new Set();
  const row = /DiskLook\(([^)]*)\)[,;)]*\s*\/\/\s*(.+)/g;
  let m;
  while ((m = row.exec(block[0]))) {
    const name = m[2].trim();
    if (seen.has(name)) continue;          // DEMO_TOUR bookends with inferno twice
    seen.add(name);
    const nums = m[1].split(',').map(v => parseFloat(v.trim()));
    if (nums.length !== LOOK_FIELDS.length || nums.some(isNaN)) continue;
    const look = {};
    LOOK_FIELDS.forEach((f, i) => { look[f] = nums[i]; });
    out.push({ name, look });
  }
  return out;
}

module.exports = { build, SHADER, PROMOTE, LOOK_FIELDS, presets };
