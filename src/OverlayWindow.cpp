#include "d4r0/OverlayWindow.h"
#include <d3d11.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <windowsx.h>

namespace d4r0 {
namespace { constexpr int kToggleTranslation = 1; constexpr int kShowOriginal = 2; }
OverlayWindow::OverlayWindow(PipelineSettings settings, RegionCache& cache) : settings_(std::move(settings)), cache_(cache) {}
OverlayWindow::~OverlayWindow() { destroy(); }

bool OverlayWindow::create(HINSTANCE instance) {
  WNDCLASSW wc{}; wc.hInstance = instance; wc.lpszClassName = L"d4r0.Overlay"; wc.lpfnWndProc = windowProc;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      wc.lpszClassName, L"d4r0", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
  if (!hwnd_) return false;
  // The overlay is excluded from WGC and other supported Windows capture paths.
  if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE)) {
    DestroyWindow(hwnd_); hwnd_ = nullptr; return false;
  }
  setFullscreenBounds();
  if (!createGraphics()) { DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
  RegisterHotKey(hwnd_, kToggleTranslation, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'T');
  RegisterHotKey(hwnd_, kShowOriginal, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'O');
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE); return true;
}
void OverlayWindow::destroy() {
  if (!hwnd_) return;
  UnregisterHotKey(hwnd_, kToggleTranslation); UnregisterHotKey(hwnd_, kShowOriginal);
  DestroyWindow(hwnd_); hwnd_ = nullptr;
}
void OverlayWindow::setFullscreenBounds() {
  const int x = GetSystemMetrics(SM_XVIRTUALSCREEN), y = GetSystemMetrics(SM_YVIRTUALSCREEN);
  const int w = GetSystemMetrics(SM_CXVIRTUALSCREEN), h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
  SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
bool OverlayWindow::createGraphics() {
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  D3D_FEATURE_LEVEL level{};
  if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
      device_.GetAddressOf(), &level, context_.GetAddressOf()))) return false;
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice; device_.As(&dxgiDevice);
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter; dxgiDevice->GetAdapter(&adapter);
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory; adapter->GetParent(IID_PPV_ARGS(&factory));
  DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = static_cast<UINT>(GetSystemMetrics(SM_CXVIRTUALSCREEN));
  desc.Height = static_cast<UINT>(GetSystemMetrics(SM_CYVIRTUALSCREEN)); desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = 2;
  desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL; desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  if (FAILED(factory->CreateSwapChainForComposition(device_.Get(), &desc, nullptr, swapChain_.GetAddressOf()))) return false;
  if (FAILED(DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(&compositionDevice_)))) return false;
  if (FAILED(compositionDevice_->CreateTargetForHwnd(hwnd_, TRUE, compositionTarget_.GetAddressOf())) ||
      FAILED(compositionDevice_->CreateVisual(rootVisual_.GetAddressOf())) ||
      FAILED(rootVisual_->SetContent(swapChain_.Get())) || FAILED(compositionTarget_->SetRoot(rootVisual_.Get())) ||
      FAILED(compositionDevice_->Commit())) return false;
  Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory;
  D2D1_FACTORY_OPTIONS options{};
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, options, d2dFactory.GetAddressOf())) ||
      FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice_.GetAddressOf())) ||
      FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext_.GetAddressOf())) ||
      FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(writeFactory_.GetAddressOf())))) return false;
  render(); return true;
}
void OverlayWindow::resize(UINT, UINT) { /* virtual-desktop overlay is recreated on display-change in the full pipeline. */ }
void OverlayWindow::setMode(DisplayMode mode) { mode_ = mode; render(); }
void OverlayWindow::render() {
  if (!swapChain_ || !context_) return;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer; if (FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) return;
  Microsoft::WRL::ComPtr<IDXGISurface> surface; backBuffer.As(&surface);
  D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> target;
  if (FAILED(d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, target.GetAddressOf()))) return;
  d2dContext_->SetTarget(target.Get()); d2dContext_->BeginDraw(); d2dContext_->Clear(D2D1::ColorF(0, 0.0F));
  if (mode_ == DisplayMode::Translation) for (const auto& region : cache_.visible()) {
    if (region.english.empty()) continue;
    const float opacity = region.lowConfidence(settings_.minimumOcrConfidence) ? settings_.lowConfidenceOpacity : 0.88F;
    const auto color = region.style.rgba;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panel; Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text;
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, opacity), &panel);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(((color >> 24) & 255) / 255.0F, ((color >> 16) & 255) / 255.0F, ((color >> 8) & 255) / 255.0F, opacity), &text);
    const D2D1_RECT_F box = D2D1::RectF(region.bounds.x - 5, region.bounds.y - 3, region.bounds.x + region.bounds.width + 5, region.bounds.y + region.bounds.height + 5);
    // A panel is the safe fallback whenever background reconstruction is not trustworthy.
    d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(box, 3, 3), panel.Get());
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    writeFactory_->CreateTextFormat(L"Segoe UI", nullptr, region.style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, region.style.fontPx, L"en-US", &format);
    format->SetTextAlignment(region.style.alignment == VisualStyle::Alignment::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : region.style.alignment == VisualStyle::Alignment::Centre ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
    const std::wstring translated(region.english.begin(), region.english.end());
    d2dContext_->DrawTextW(translated.data(), static_cast<UINT32>(translated.size()), format.Get(), box, text.Get());
    if (region.lowConfidence(settings_.minimumOcrConfidence)) {
      Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dots; D2D1_STROKE_STYLE_PROPERTIES s = D2D1::StrokeStyleProperties(); s.dashStyle = D2D1_DASH_STYLE_DOT;
      Microsoft::WRL::ComPtr<ID2D1Factory> factory; d2dContext_->GetFactory(&factory); factory->CreateStrokeStyle(s, nullptr, 0, &dots);
      d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(box, 3, 3), text.Get(), 1.0F, dots.Get());
    }
  }
  d2dContext_->EndDraw(); d2dContext_->SetTarget(nullptr);
  swapChain_->Present(1, 0); if (compositionDevice_) compositionDevice_->Commit();
}
LRESULT OverlayWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_HOTKEY: if (wParam == kToggleTranslation) setMode(mode_ == DisplayMode::Translation ? DisplayMode::Original : DisplayMode::Translation);
      else if (wParam == kShowOriginal) setMode(DisplayMode::Original); return 0;
    case WM_DISPLAYCHANGE: setFullscreenBounds(); render(); return 0;
    case WM_DPICHANGED: SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); return 0;
    case WM_ERASEBKGND: return 1;
  } return DefWindowProcW(hwnd_, message, wParam, lParam);
}
LRESULT CALLBACK OverlayWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) { self = static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
  return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace d4r0
