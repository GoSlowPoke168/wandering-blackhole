#pragma once
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <wrl/client.h>
#include <string>

using Microsoft::WRL::ComPtr;

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

// Per-pixel-shader parameters. Layout mirrored by the HLSL cbuffer; keep 16-byte multiples.
struct alignas(16) LensParams {
  float res[2];       // window size in px
  float center[2];    // circle centre in window px (top-down)
  float radius;       // px
  float mag;          // magnification inside the circle
  float ring;         // ring thickness px
  float pad;
};
static_assert(sizeof(LensParams) % 16 == 0, "cbuffer size");

// One overlay per display output: a click-through, capture-excluded DirectComposition
// window covering the whole monitor, fed by a Desktop Duplication of that same output.
// Everything GPU-side lives on the adapter that owns the output.
class Overlay {
public:
  Overlay(ComPtr<IDXGIAdapter1> adapter, ComPtr<IDXGIOutput1> output, HINSTANCE hinst);
  ~Overlay();
  Overlay(const Overlay&) = delete;
  Overlay& operator=(const Overlay&) = delete;

  bool init(const char* hlsl, std::wstring* err);

  // Pull the newest desktop frame, waiting up to `timeoutMs` for one. Returns true when a
  // new frame landed. Duplication loss (sleep, lock screen, mode change) blanks and
  // retries on its own.
  bool acquire(UINT timeoutMs = 0);
  // True until a fresh frame arrives after (re)starting duplication: never lens a stale one.
  bool haveFrame() const { return haveFrame_; }
  bool captureLive() const { return dupl_ != nullptr; }
  HRESULT lastDuplicateResult() const { return lastDupHr_; }
  // Set once when capture is lost; the caller presents a blank frame and clears it.
  bool takeNeedsBlank() { bool b = needBlank_; needBlank_ = false; return b; }

  // Draw one frame limited to `dirty` (window px); nullptr = whole window.
  void draw(const LensParams& p, const RECT* dirty);
  void presentBlank();

  HANDLE waitable() const { return waitable_; }
  const RECT& rect() const { return rect_; }
  int width() const { return rect_.right - rect_.left; }
  int height() const { return rect_.bottom - rect_.top; }
  int captureWidth() const { return capW_; }
  int captureHeight() const { return capH_; }
  bool affinityOk() const { return affinityOk_; }
  const std::wstring& name() const { return name_; }

  ID3D11Device* device() const { return dev_.Get(); }
  ID3D11DeviceContext1* context() const { return ctx_.Get(); }
  IDXGIFactory2* factory() const { return factory_.Get(); }
  // Copy a region of the captured desktop to CPU memory (BGRA). Stalls the pipeline; use rarely.
  bool readCapture(int x, int y, int w, int h, unsigned char* outBGRA);

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
  std::wstring name_;
  bool affinityOk_ = false;

  ComPtr<ID3D11Device> dev_;
  ComPtr<ID3D11DeviceContext1> ctx_;
  ComPtr<IDCompositionDevice> dcomp_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual> visual_;
  ComPtr<IDXGISwapChain2> swap_;
  ComPtr<ID3D11RenderTargetView> rtv_;
  HANDLE waitable_ = nullptr;
  bool firstPresent_ = true;

  ComPtr<IDXGIOutputDuplication> dupl_;
  ComPtr<ID3D11Texture2D> cap_;
  ComPtr<ID3D11ShaderResourceView> capSrv_;
  int capW_ = 0, capH_ = 0;
  bool haveFrame_ = false, needBlank_ = false;
  ULONGLONG lastRetry_ = 0;
  HRESULT lastDupHr_ = S_OK;

  ComPtr<ID3D11VertexShader> vs_;
  ComPtr<ID3D11PixelShader> ps_;
  ComPtr<ID3D11SamplerState> sampler_;
  ComPtr<ID3D11RasterizerState> raster_;
  ComPtr<ID3D11Buffer> cbuf_;
  ComPtr<ID3D11Texture2D> staging_;
  int stagingW_ = 0, stagingH_ = 0;
};

// Set by the window procedure when displays change; the host tears down and rebuilds.
extern volatile bool g_displayChanged;
