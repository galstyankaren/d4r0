#pragma once
#include "d4r0/RegionCache.h"
#include "d4r0/Settings.h"
#include "d4r0/Types.h"
#include "d4r0/DiagnosticSession.h"
#include "d4r0/PanelLayout.h"
#include <atomic>
#include <d3d11.h>
#include <windows.h>
#include <wrl/client.h>
#include <unordered_map>
#include <utility>

struct ID3D11DeviceContext;
struct IDXGISwapChain1;
struct IDCompositionDevice;
struct IDCompositionTarget;
struct IDCompositionVisual;
struct ID2D1Device;
struct ID2D1DeviceContext;
struct IDWriteFactory;
class ID2D1Bitmap1;
struct IDWriteTextLayout;

namespace d4r0 {
class OverlayWindow {
 public:
  OverlayWindow(PipelineSettings settings, RegionCache& cache, DiagnosticSession& diagnostics);
  ~OverlayWindow();
  OverlayWindow(const OverlayWindow&) = delete;
  bool create(HINSTANCE instance);
  void destroy();
  void setMode(DisplayMode mode);
  void setDiagnostics(bool enabled);
  [[nodiscard]] bool diagnosticsEnabled() const { return showDiagnostics_; }
  [[nodiscard]] DisplayMode mode() const { return mode_; }
  [[nodiscard]] HWND hwnd() const { return hwnd_; }
  void render();
  void setStatus(std::wstring status);
  void setSourceCapture(class WindowsGraphicsCapture* capture) { capture_ = capture; }
  LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
 private:
  bool createGraphics();
  void releaseGraphics();
  void recoverGraphics();
  void resize(UINT width, UINT height);
  void setFullscreenBounds();
  static LRESULT CALLBACK windowProc(HWND, UINT, WPARAM, LPARAM);
  PipelineSettings settings_; RegionCache& cache_; DiagnosticSession& diagnostics_;
  DisplayMode mode_{DisplayMode::Translation}; HWND hwnd_{};
  std::mutex statusMutex_;
  std::atomic<std::uint64_t> statusVersion_{};
  std::wstring status_{L"Starting local translation..."};
  bool showDiagnostics_{};
  double renderMs_{};
  Microsoft::WRL::ComPtr<ID3D11Device> device_; Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_; Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget_; Microsoft::WRL::ComPtr<IDCompositionVisual> rootVisual_;
  Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_; Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
  Microsoft::WRL::ComPtr<IDWriteFactory> writeFactory_;
  class WindowsGraphicsCapture* capture_{};
  std::unordered_map<std::uint64_t,std::pair<std::uint64_t,bool>> lightBackgrounds_;
  struct CachedPanel {
    PanelPlacement placement;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  };
  std::unordered_map<std::uint64_t,CachedPanel> panels_;
  std::uint64_t layoutGeneration_{}, renderedCacheGeneration_{}, renderedFrameRevision_{}, renderedStatusVersion_{};
  bool forceRender_{true};
  bool renderedHasPanels_{};
};
} // namespace d4r0
