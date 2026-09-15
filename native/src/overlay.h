#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>
#include "renderer.h"
#include "hud.h"

using Microsoft::WRL::ComPtr;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// One overlay per display output: a click-through, capture-excluded DirectComposition
// window covering the whole monitor, fed by a Desktop Duplication of that same output.
// Created on the UI thread (it owns a window); drawn from the render thread.
class Overlay {
public:
  Overlay(ComPtr<IDXGIAdapter1> adapter, ComPtr<IDXGIOutput1> output, HINSTANCE hinst);
  ~Overlay();
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  bool init(const std::wstring& hlslPath, std::wstring* err);

  // Pull the newest desktop frame, waiting up to `timeoutMs` for one. Duplication loss
  // (sleep, lock screen, mode change) blanks and retries. We are excluded from the capture
  // image but not from DWM's damage tracking, so each of our own presents comes back as a
  // "new" frame: one whose dirty rects all sit inside what we last presented is an Echo -
  // copied (cheap, keeps the lens fresh) but not worth a redraw on its own.
  enum Frame { NoFrame, Echo, Real };
  Frame acquire(UINT timeoutMs = 0);
  void stopCapture();                       // hidden: release the duplication entirely
  void restartCapture() { stopCapture(); lastRetry_ = 0; }
  bool haveFrame() const { return haveFrame_; }
  bool captureLive() const { return dupl_ != nullptr; }
  bool takeNeedsBlank() { bool b = needBlank_; needBlank_ = false; return b; }
  HRESULT lastDuplicateResult() const { return lastDupHr_; }
  void setDebug(bool on) { debug_ = on; }
  const std::wstring& lastMeta() const { return lastMeta_; }   // debug: how the last frame was classified

  // Take the swapchain's present slot, or report that a present is still in flight so the
  // caller can skip a frame it could not show anyway. The slot is a semaphore: taking one
  // and not presenting would leak it, and with a maximum frame latency of 1 a single leak
  // wedges the swapchain permanently - so a slot taken here is remembered and reused.
  bool acquireSlot(DWORD timeoutMs = 0);
  HRESULT lastPresentHr() const { return lastPresentHr_; }

  // Clear `dirty`, run the shader inside `scissor` (skipped when null), draw the HUD if
  // asked, present `dirty`. All rects in window px.
  void draw(const Uniforms& u, const RECT* scissor, const RECT& dirty, bool withHud);
  void presentBlank();
  Hud* hud();                               // created on first use

  HANDLE waitable() const { return waitable_; }
  const RECT& rect() const { return rect_; }
  int width() const { return rect_.right - rect_.left; }
  int height() const { return rect_.bottom - rect_.top; }
  float scale() const { return scale_; }
  int captureWidth() const { return capW_; }
  int captureHeight() const { return capH_; }
  bool affinityOk() const { return affinityOk_; }
  const std::wstring& name() const { return name_; }
  LUID adapterLuid() const { return luid_; }

private:
  static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
  bool startDuplication();
  void loseDuplication();

  ComPtr<IDXGIAdapter1> adapter_;
  ComPtr<IDXGIOutput1> output_;
  ComPtr<IDXGIFactory2> factory_;
  HINSTANCE hinst_;
  HWND hwnd_ = nullptr;
  RECT rect_{};
  LUID luid_{};
  std::wstring name_;
  float scale_ = 1;
  bool affinityOk_ = false;

  ComPtr<ID3D11Device> dev_;
  ComPtr<ID3D11DeviceContext1> ctx_;
  ComPtr<IDCompositionDevice> dcomp_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual> visual_;
  ComPtr<IDXGISwapChain2> swap_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  HANDLE waitable_ = nullptr;
  bool firstPresent_ = true, slotHeld_ = false;
  HRESULT lastPresentHr_ = S_OK;

  ComPtr<IDXGIOutputDuplication> dupl_;
  ComPtr<ID3D11Texture2D> cap_;
  ComPtr<ID3D11ShaderResourceView> capSrv_;
  int capW_ = 0, capH_ = 0;
  bool haveFrame_ = false, needBlank_ = false;
  ULONGLONG lastRetry_ = 0;
  HRESULT lastDupHr_ = S_OK;
  // DWM reports our own presents back as damage outset by a pixel, sometimes a present or
  // two late, so the echo test looks at the last few presented rects with a little slack.
  RECT presented_[3]{}; int presentedIdx_ = 0;
  void notePresented(const RECT& r) { presented_[presentedIdx_++ % 3] = r; }
  bool isEcho(const RECT& dirty) const;
  std::vector<unsigned char> meta_;
  bool debug_ = false;
  std::wstring lastMeta_;

  Renderer renderer_;
  std::unique_ptr<Hud> hud_;
};

// Set by the window procedure when displays change; the host tears down and rebuilds.
extern volatile bool g_displayChanged;
