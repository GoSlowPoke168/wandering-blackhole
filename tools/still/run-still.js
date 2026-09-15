'use strict';
// Shader-port check. Renders the same cases through the patched GLSL (WebGL, in this
// Electron process) and through native\bhp-spike.exe --still (blackhole.hlsl), composites
// both over desktop.png, and diffs them. Run from the repo root:
//     node_modules\.bin\electron tools\still\run-still.js
// Pass: mean abs error <= 1/255 and <= 0.5% of pixels differ by more than 8/255 in any
// channel. Everything lands in tools\still\out.
const { app, BrowserWindow, ipcMain } = require('electron');
const path = require('path');
const fs = require('fs');
const { spawnSync } = require('child_process');

const ROOT = path.join(__dirname, '..', '..');
const { build, presets } = require(path.join(ROOT, 'src', 'shader-patch.js'));
const OUT = path.join(__dirname, 'out');
const IMG = path.join(__dirname, 'desktop.png');
const EXE = path.join(ROOT, 'native', 'bhp-spike.exe');
const MEAN_MAX = 1.0, BAD_PCT_MAX = 0.5, BAD_THRESHOLD = 8;

function cases() {
  const defaults = build().defaults, looks = presets();
  const out = [];
  for (const level of [0.05, 0.3, 1.0])
    for (const lookName of ['inferno', 'm87* donut'])
      for (const c of [[0.5, 0.4], [0.22, 0.22]]) {
        const look = looks.find(l => l.name === lookName);
        if (!look) throw new Error('preset missing: ' + lookName);
        out.push({
          name: `L${level}_${lookName.replace(/[^a-z0-9]+/gi, '-')}_${c[0]}-${c[1]}`,
          u: Object.assign({}, defaults, look.look,
            { iTime: 5.0, TOKEN_LEVEL: level, uCenterPin: 1, uDriftTime: 12.34, uCenter: c, uDesktopXform: [1, 1, 0, 0] }),
        });
      }
  return out;
}
const caseText = u => Object.entries(u).map(([k, v]) => `${k} ${Array.isArray(v) ? v.join(' ') : v}`).join('\n') + '\n';

function readRaw(file) {
  const b = fs.readFileSync(file);
  return { w: b.readUInt32LE(0), h: b.readUInt32LE(4), rgba: b.subarray(8) };
}
const dataUrlToBuffer = d => Buffer.from(d.split(',')[1], 'base64');

app.whenReady().then(async () => {
  fs.mkdirSync(path.join(OUT, 'cases'), { recursive: true });
  const all = cases(), only = process.argv.slice(2).filter(a => !a.startsWith('-'));
  const run = only.length ? all.filter(c => only.some(o => c.name.includes(o))) : all;

  const win = new BrowserWindow({ show: false, webPreferences: { nodeIntegration: true, contextIsolation: false, offscreen: false } });
  await win.loadFile(path.join(__dirname, 'still.html'));
  const render = payload => new Promise((res, rej) => {
    ipcMain.once('still-done', (_e, r) => r.error ? rej(new Error(r.error)) : res(r));
    win.webContents.send('still-render', payload);
  });

  const rows = []; let failed = 0;
  for (const c of run) {
    const caseFile = path.join(OUT, 'cases', c.name + '.txt');
    fs.writeFileSync(caseFile, caseText(c.u));
    const nativeRaw = path.join(OUT, c.name + '.native.raw');
    const nat = spawnSync(EXE, ['--still', IMG, caseFile, nativeRaw, path.join(OUT, c.name + '.native.png')], { encoding: 'utf8' });
    if (nat.status !== 0) { console.log(`${c.name}: native render FAILED\n${nat.stdout}${nat.stderr}`); failed++; continue; }
    const native = readRaw(nativeRaw);

    const r = await render({ u: c.u, image: IMG, nativeRgba: native.rgba, nativeW: native.w, nativeH: native.h });
    fs.writeFileSync(path.join(OUT, c.name + '.webgl.raw'), Buffer.concat([Buffer.from(new Uint32Array([r.w, r.h]).buffer), Buffer.from(r.webglRgba)]));
    for (const [k, f] of [['webglComposite', '.webgl.composite.png'], ['nativeComposite', '.native.composite.png'], ['heat', '.diff.png']])
      fs.writeFileSync(path.join(OUT, c.name + f), dataUrlToBuffer(r[k]));
    const ok = r.mean <= MEAN_MAX && r.badPct <= BAD_PCT_MAX;
    if (!ok) failed++;
    rows.push({ name: c.name, mean: r.mean, max: r.max, badPct: r.badPct, ok });
    console.log(`${ok ? 'PASS' : 'FAIL'}  ${c.name.padEnd(36)} mean ${r.mean.toFixed(3)}  max ${r.max}  >${BAD_THRESHOLD}: ${r.badPct.toFixed(3)}%`);
  }
  fs.writeFileSync(path.join(OUT, 'summary.json'), JSON.stringify(rows, null, 2));
  console.log(`\n${rows.length - failed}/${run.length} cases pass (mean <= ${MEAN_MAX}, >${BAD_THRESHOLD} <= ${BAD_PCT_MAX}%)`);
  app.exit(failed ? 1 : 0);
}).catch(e => { console.error(e); app.exit(2); });
