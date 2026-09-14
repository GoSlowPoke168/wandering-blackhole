// THROWAWAY - Milestone 0 capture probe. Measures Chromium desktopCapturer as a
// WebGL texture source. Deliberately does NOT exclude itself from capture: the
// probe captures its own counter blocks, which is how latency is measured.
const { app, BrowserWindow, desktopCapturer, session, screen } = require('electron')

// Chromium picks DXGI/Desktop-Duplication for screen capture by default on Windows.
// WGC=1 forces the Windows.Graphics.Capture path instead, so we can measure both.
if (process.env.WGC === '1') {
  app.commandLine.appendSwitch('enable-features', 'WebRtcAllowWgcScreenCapturer,WebRtcAllowWgcDesktopCapturer')
}
app.commandLine.appendSwitch('disable-frame-rate-limit')

app.whenReady().then(() => {
  session.defaultSession.setDisplayMediaRequestHandler((req, callback) => {
    desktopCapturer.getSources({ types: ['screen'] }).then(sources => callback({ video: sources[0] }))
  }, { useSystemPicker: false })

  const b = screen.getPrimaryDisplay().bounds
  const win = new BrowserWindow({
    x: b.x, y: b.y, width: b.width, height: b.height,
    frame: false, resizable: false,
    webPreferences: { nodeIntegration: true, contextIsolation: false, backgroundThrottling: false }
  })
  win.setAlwaysOnTop(true, 'screen-saver')
  win.loadFile(process.env.PAGE || 'index.html', { search: (process.env.WGC === '1' ? 'wgc=1&' : '') + 'secs=' + (process.env.SECS || '20') + '&each=' + (process.env.EACH || '3') })
  win.webContents.on('before-input-event', (e, i) => { if (i.key === 'Escape') app.quit() })
})

app.on('window-all-closed', () => app.quit())
