#include "d4r0/OverlayWindow.h"
#include "d4r0/DebugLog.h"
#include "d4r0/PanelLayout.h"
#include "d4r0/WindowsGraphicsCapture.h"
#include <d3d11.h>
#include <dcomp.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <windowsx.h>

#include <string>
#include <winrt/base.h>
#include <chrono>
#include <cwchar>
#include <algorithm>

namespace d4r0 {
namespace {
constexpr int kToggleTranslation = 1;
constexpr int kShowOriginal = 2;
constexpr int kExit = 3;
constexpr int kDiagnostics = 4;
bool deviceLost(HRESULT result) {
  return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
      result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
}
}
OverlayWindow::OverlayWindow(PipelineSettings settings, RegionCache& cache, DiagnosticSession& diagnostics)
    : settings_(std::move(settings)), cache_(cache), diagnostics_(diagnostics),
      showDiagnostics_(settings_.showDiagnostics) {}
OverlayWindow::~OverlayWindow() { destroy(); }
void OverlayWindow::setDiagnostics(bool enabled) {
  showDiagnostics_ = enabled;
  diagnostics_.setEnabled(enabled);
  render();
}

bool OverlayWindow::create(HINSTANCE instance) {
  WNDCLASSW wc{}; wc.hInstance = instance; wc.lpszClassName = L"d4r0.Overlay"; wc.lpfnWndProc = windowProc;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      wc.lpszClassName, L"d4r0", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
  if (!hwnd_) { debugLog("CreateWindowEx failed: " + std::to_string(GetLastError())); return false; }
  // The overlay is excluded from WGC and other supported Windows capture paths.
  if (!SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE)) {
    debugLog("SetWindowDisplayAffinity failed: " + std::to_string(GetLastError()));
    DestroyWindow(hwnd_); hwnd_ = nullptr; return false;
  }
  setFullscreenBounds();
  if (!createGraphics()) { debugLog("D3D11/DirectComposition setup failed"); DestroyWindow(hwnd_); hwnd_ = nullptr; return false; }
  auto registerShortcut = [&](int id, const std::wstring& value) {
    const auto binding = parseShortcut(value);
    if (!binding) return false;
    UINT modifiers = MOD_NOREPEAT;
    if (binding->modifiers & ShortcutCtrl) modifiers |= MOD_CONTROL;
    if (binding->modifiers & ShortcutShift) modifiers |= MOD_SHIFT;
    if (binding->modifiers & ShortcutAlt) modifiers |= MOD_ALT;
    return RegisterHotKey(hwnd_,id,modifiers,binding->key) != FALSE;
  };
  if (!registerShortcut(kToggleTranslation,settings_.toggleShortcut) ||
      !registerShortcut(kShowOriginal,settings_.originalShortcut) ||
      !registerShortcut(kDiagnostics,settings_.diagnosticsShortcut) ||
      !RegisterHotKey(hwnd_, kExit, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'Q')) {
    debugLog("A configured global hotkey is already in use"); destroy(); return false;
  }
  SetTimer(hwnd_,1,100,nullptr);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE); return true;
}
void OverlayWindow::destroy() {
  if (!hwnd_) return;
  UnregisterHotKey(hwnd_, kToggleTranslation); UnregisterHotKey(hwnd_, kShowOriginal);
  UnregisterHotKey(hwnd_, kDiagnostics); UnregisterHotKey(hwnd_, kExit);
  DestroyWindow(hwnd_); hwnd_ = nullptr;
}
void OverlayWindow::setFullscreenBounds() {
  struct Search { unsigned wanted, index{}; RECT bounds{}; } search{settings_.captureMonitorIndex};
  EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR, HDC, LPRECT rectangle, LPARAM value)->BOOL {
    auto& search = *reinterpret_cast<Search*>(value);
    if (search.index++ == search.wanted) { search.bounds = *rectangle; return FALSE; }
    return TRUE;
  },reinterpret_cast<LPARAM>(&search));
  const auto& r = search.bounds;
  SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right-r.left, r.bottom-r.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
bool OverlayWindow::createGraphics() {
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
  D3D_FEATURE_LEVEL level{};
  auto createDevice = [&] {
    return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
        device_.GetAddressOf(), &level, context_.GetAddressOf());
  };
  auto result = createDevice();
#if defined(_DEBUG)
  if (result == DXGI_ERROR_SDK_COMPONENT_MISSING) {
    flags &= ~D3D11_CREATE_DEVICE_DEBUG;
    result = createDevice();
  }
#endif
  if (FAILED(result)) return false;
  Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice; device_.As(&dxgiDevice);
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter; dxgiDevice->GetAdapter(&adapter);
  Microsoft::WRL::ComPtr<IDXGIFactory2> factory; adapter->GetParent(IID_PPV_ARGS(&factory));
  RECT client{}; GetClientRect(hwnd_,&client);
  DXGI_SWAP_CHAIN_DESC1 desc{}; desc.Width = client.right;
  desc.Height = client.bottom; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
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
void OverlayWindow::resize(UINT width, UINT height) {
  if (!swapChain_ || !width || !height) return;
  lightBackgrounds_.clear();
  panels_.clear(); forceRender_ = true;
  d2dContext_->SetTarget(nullptr);
  const auto result = swapChain_->ResizeBuffers(0,width,height,DXGI_FORMAT_UNKNOWN,0);
  if (deviceLost(result)) recoverGraphics();
  else if (FAILED(result)) debugLog("Overlay resize failed");
}
void OverlayWindow::releaseGraphics() {
  lightBackgrounds_.clear();
  panels_.clear(); forceRender_ = true;
  writeFactory_.Reset(); d2dContext_.Reset(); d2dDevice_.Reset();
  rootVisual_.Reset(); compositionTarget_.Reset(); compositionDevice_.Reset();
  swapChain_.Reset(); context_.Reset(); device_.Reset();
}
void OverlayWindow::recoverGraphics() {
  debugLog("Recreating overlay graphics after device loss");
  releaseGraphics();
  if (!createGraphics()) {
    debugLog("Overlay graphics recovery failed");
    PostMessageW(hwnd_,WM_CLOSE,0,0);
  }
}
void OverlayWindow::setStatus(std::wstring status) {
  std::scoped_lock lock(statusMutex_);
  if (status_ != status) { status_ = std::move(status); ++statusVersion_; }
}
void OverlayWindow::setMode(DisplayMode mode) {
  mode_ = mode;
  debugLog(mode == DisplayMode::Translation ? "Display mode: translation" : "Display mode: original");
  render();
}
void OverlayWindow::render() {
  const auto renderStart = std::chrono::steady_clock::now();
  if (!swapChain_ || !context_) return;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
  auto result = swapChain_->GetBuffer(0,IID_PPV_ARGS(&backBuffer));
  if (FAILED(result)) { if (deviceLost(result)) recoverGraphics(); else debugLog("Cannot acquire overlay buffer"); return; }
  Microsoft::WRL::ComPtr<IDXGISurface> surface;
  result = backBuffer.As(&surface);
  if (FAILED(result)) { debugLog("Cannot access overlay surface"); return; }
  D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> target;
  result = d2dContext_->CreateBitmapFromDxgiSurface(surface.Get(),&properties,target.GetAddressOf());
  if (FAILED(result)) {
    if (result == D2DERR_RECREATE_TARGET || deviceLost(result)) recoverGraphics();
    else debugLog("Cannot create overlay render target");
    return;
  }
  d2dContext_->SetTarget(target.Get()); d2dContext_->BeginDraw(); d2dContext_->Clear(D2D1::ColorF(0, 0.0F));
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> sourceBitmap;
  Microsoft::WRL::ComPtr<ID2D1Effect> sourceBlur;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> sharedTexture;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> colorSample;
  std::uint64_t capturedRevision{};
  if (mode_ == DisplayMode::Translation && capture_) {
    const auto frame = capture_->latestFrame();
    capturedRevision = frame.revision;
    if (frame.texture) {
      Microsoft::WRL::ComPtr<IDXGIResource> sourceResource;
      HANDLE sharedHandle{};
      Microsoft::WRL::ComPtr<IDXGISurface> sharedSurface;
      if (SUCCEEDED(frame.texture.As(&sourceResource)) &&
          SUCCEEDED(sourceResource->GetSharedHandle(&sharedHandle)) &&
          SUCCEEDED(device_->OpenSharedResource(sharedHandle,IID_PPV_ARGS(&sharedTexture))) &&
          SUCCEEDED(sharedTexture.As(&sharedSurface))) {
        const auto bitmapProperties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));
        if (SUCCEEDED(d2dContext_->CreateBitmapFromDxgiSurface(sharedSurface.Get(),&bitmapProperties,&sourceBitmap)) &&
            SUCCEEDED(d2dContext_->CreateEffect(CLSID_D2D1GaussianBlur,&sourceBlur))) {
          sourceBlur->SetInput(0,sourceBitmap.Get());
          sourceBlur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION,14.0F);
          sourceBlur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE,D2D1_BORDER_MODE_HARD);
        } else { sourceBitmap.Reset(); sourceBlur.Reset(); }
      }
      if (sourceBlur) {
        D3D11_TEXTURE2D_DESC sample{};
        sample.Width=2; sample.Height=2; sample.MipLevels=1; sample.ArraySize=1;
        sample.Format=DXGI_FORMAT_B8G8R8A8_UNORM; sample.SampleDesc.Count=1;
        sample.Usage=D3D11_USAGE_STAGING; sample.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        device_->CreateTexture2D(&sample,nullptr,&colorSample);
      }
    }
  }
  const auto cacheGeneration = cache_.generation();
  if (layoutGeneration_ != cacheGeneration) { panels_.clear(); layoutGeneration_ = cacheGeneration; }
  auto regions = cache_.visible();
  std::sort(regions.begin(),regions.end(),[](const auto& a,const auto& b) {
    if (a.bounds.y != b.bounds.y) return a.bounds.y < b.bounds.y;
    return a.bounds.x < b.bounds.x;
  });
  std::vector<Rect> placedPanels;
  RECT client{}; GetClientRect(hwnd_,&client);
  D3D11_TEXTURE2D_DESC sourceDescription{};
  if (sharedTexture) sharedTexture->GetDesc(&sourceDescription);
  if (mode_ == DisplayMode::Translation) for (const auto& region : regions) {
    if (region.english.empty()) continue;
    const float opacity = region.lowConfidence(settings_.minimumOcrConfidence) ? settings_.lowConfidenceOpacity : 0.88F;
    const auto color = region.style.rgba;
    bool lightBackground=false;
    if (sourceBlur && colorSample && sourceDescription.Width && sourceDescription.Height) {
      auto known=lightBackgrounds_.find(region.stableId);
      if (known == lightBackgrounds_.end() || known->second.first != region.revision) {
        const int left=std::clamp(int(region.bounds.x)-5,0,int(sourceDescription.Width)-1);
        const int top=std::clamp(int(region.bounds.y)-5,0,int(sourceDescription.Height)-1);
        const int right=std::clamp(int(region.bounds.x+region.bounds.width)+5,0,int(sourceDescription.Width)-1);
        const int bottom=std::clamp(int(region.bounds.y+region.bounds.height)+5,0,int(sourceDescription.Height)-1);
        const int coordinates[4][2]={{left,top},{right,top},{left,bottom},{right,bottom}};
        for (unsigned i=0;i<4;++i) {
          const auto x=UINT(coordinates[i][0]), y=UINT(coordinates[i][1]);
          const D3D11_BOX source{x,y,0,x+1,y+1,1};
          context_->CopySubresourceRegion(colorSample.Get(),0,i%2,i/2,0,sharedTexture.Get(),0,&source);
        }
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context_->Map(colorSample.Get(),0,D3D11_MAP_READ,0,&mapped))) {
          float luminance{};
          for (unsigned y=0;y<2;++y) for (unsigned x=0;x<2;++x) {
            const auto* pixel=static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.RowPitch+x*4;
            luminance+=0.0722F*pixel[0]+0.7152F*pixel[1]+0.2126F*pixel[2];
          }
          context_->Unmap(colorSample.Get(),0);
          lightBackground=luminance/4>145;
          if (lightBackgrounds_.size()>512) lightBackgrounds_.clear();
          lightBackgrounds_[region.stableId]={region.revision,lightBackground};
        }
      } else lightBackground=known->second.second;
    }
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> panel; Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text;
    const float tint=sourceBlur ? std::min(opacity,lightBackground ? 0.82F : 0.68F) : opacity;
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(lightBackground ? 1.0F : 0.0F,
        lightBackground ? 1.0F : 0.0F,lightBackground ? 1.0F : 0.0F,tint), &panel);
    d2dContext_->CreateSolidColorBrush(lightBackground ? D2D1::ColorF(0,0,0,opacity) :
        D2D1::ColorF(((color >> 24) & 255) / 255.0F, ((color >> 16) & 255) / 255.0F,
            ((color >> 8) & 255) / 255.0F, opacity), &text);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    PanelPlacement placement;
    if (const auto cached = panels_.find(region.stableId); cached != panels_.end()) {
      placement = cached->second.placement;
      layout = cached->second.layout;
    } else {
      const auto translated = winrt::to_hstring(region.english);
      std::vector<Rect> occupied;
      for (const auto& neighbor : regions)
        if (neighbor.stableId != region.stableId) occupied.push_back(neighbor.bounds);
      occupied.insert(occupied.end(),placedPanels.begin(),placedPanels.end());
      Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
      placement = fitPanel(region.bounds,occupied,{0,0,float(client.right),float(client.bottom)},
          region.style.fontPx,[&](float width,float font) {
        format.Reset(); layout.Reset();
        writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,
            region.style.bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,font,L"en-US",&format);
        if (!format) return 10000.0F;
        format->SetTextAlignment(region.style.alignment == VisualStyle::Alignment::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING : region.style.alignment == VisualStyle::Alignment::Centre ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
        writeFactory_->CreateTextLayout(translated.data(),UINT32(translated.size()),format.Get(),
                                         width,10000.0F,&layout);
        if (!layout) return 10000.0F;
        DWRITE_TEXT_METRICS metrics{};
        layout->GetMetrics(&metrics);
        return metrics.width>width+0.5F ? 10000.0F : metrics.height;
      });
      if (!placement.bounds.width) layout.Reset();
      panels_.emplace(region.stableId,CachedPanel{placement,layout});
    }
    if (!placement.bounds.width || !layout) {
      if (diagnostics_.overlayDue()) diagnostics_.event("panelRejected",region.revision,
          "\"sourceId\":"+std::to_string(region.sourceId)+
          ",\"stableId\":"+std::to_string(region.stableId)+
          ",\"text\":"+DiagnosticSession::quote(region.german)+
          ",\"x\":"+std::to_string(region.bounds.x)+
          ",\"y\":"+std::to_string(region.bounds.y)+
          ",\"reason\":\"noFreeSpace\"");
      continue;
    }
    const auto& p=placement.bounds;
    placedPanels.push_back(p);
    const D2D1_RECT_F sourceBox = D2D1::RectF(region.bounds.x,region.bounds.y,
        region.bounds.x+region.bounds.width,region.bounds.y+region.bounds.height);
    const D2D1_RECT_F box = D2D1::RectF(p.x,p.y,p.x+p.width,p.y+p.height);
    const bool sourceCovered = box.left<=sourceBox.left && box.top<=sourceBox.top &&
        box.right>=sourceBox.right && box.bottom>=sourceBox.bottom;
    if (sourceBlur) {
      if (!sourceCovered) {
        d2dContext_->PushAxisAlignedClip(sourceBox,D2D1_ANTIALIAS_MODE_ALIASED);
        d2dContext_->DrawImage(sourceBlur.Get(),D2D1::Point2F(sourceBox.left,sourceBox.top),sourceBox);
        d2dContext_->PopAxisAlignedClip();
      }
      d2dContext_->PushAxisAlignedClip(box,D2D1_ANTIALIAS_MODE_ALIASED);
      d2dContext_->DrawImage(sourceBlur.Get(),D2D1::Point2F(box.left,box.top),box);
      d2dContext_->PopAxisAlignedClip();
    }
    if (!sourceCovered) d2dContext_->FillRectangle(sourceBox,panel.Get());
    d2dContext_->FillRoundedRectangle(D2D1::RoundedRect(box, 3, 3), panel.Get());
    d2dContext_->DrawTextLayout(D2D1::Point2F(box.left+5,box.top+4),layout.Get(),text.Get());
    if (diagnostics_.overlayDue()) diagnostics_.event("panel",region.revision,
        "\"sourceId\":"+std::to_string(region.sourceId)+
        ",\"stableId\":"+std::to_string(region.stableId)+
        ",\"x\":"+std::to_string(p.x)+
        ",\"y\":"+std::to_string(p.y)+",\"width\":"+std::to_string(p.width)+
        ",\"height\":"+std::to_string(p.height)+",\"font\":"+std::to_string(placement.fontPx)+
        ",\"blurred\":"+(sourceBlur ? "true" : "false")+
        ",\"lightBackground\":"+(lightBackground ? "true" : "false"));
    if (region.lowConfidence(settings_.minimumOcrConfidence)) {
      Microsoft::WRL::ComPtr<ID2D1StrokeStyle> dots; D2D1_STROKE_STYLE_PROPERTIES s = D2D1::StrokeStyleProperties(); s.dashStyle = D2D1_DASH_STYLE_DOT;
      Microsoft::WRL::ComPtr<ID2D1Factory> factory; d2dContext_->GetFactory(&factory); factory->CreateStrokeStyle(s, nullptr, 0, &dots);
      d2dContext_->DrawRoundedRectangle(D2D1::RoundedRect(box, 3, 3), text.Get(), 1.0F, dots.Get());
    }
  }
  if (mode_ == DisplayMode::Translation && showDiagnostics_) {
    std::wstring status;
    { std::scoped_lock lock(statusMutex_); status = status_; }
    wchar_t renderText[32]{}; swprintf_s(renderText,L" | render %.1fms",renderMs_);
    status += renderText;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background, foreground;
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(0,0,0,0.85F),&background);
    d2dContext_->CreateSolidColorBrush(D2D1::ColorF(1,1,1,1),&foreground);
    Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    writeFactory_->CreateTextFormat(L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,16,L"en-US",&format);
    const auto panel = D2D1::RectF(12,12,std::max(400.0F,float(client.right)-12),60);
    d2dContext_->FillRectangle(panel,background.Get());
    d2dContext_->DrawTextW(status.data(),UINT32(status.size()),format.Get(),
        D2D1::RectF(20,18,std::max(400.0F,float(client.right)-20),56),foreground.Get());
  }
  const auto drawResult = d2dContext_->EndDraw(); d2dContext_->SetTarget(nullptr);
  if (drawResult == D2DERR_RECREATE_TARGET) { recoverGraphics(); return; }
  if (FAILED(drawResult)) { debugLog("Overlay drawing failed"); return; }
  if (diagnostics_.overlayDue()) {
    D3D11_TEXTURE2D_DESC desc{}; backBuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ; desc.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
    if (SUCCEEDED(device_->CreateTexture2D(&desc,nullptr,&staging))) {
      context_->CopyResource(staging.Get(),backBuffer.Get());
      D3D11_MAPPED_SUBRESOURCE mapped{};
      if (SUCCEEDED(context_->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped))) {
        std::vector<std::uint8_t> pixels(std::size_t(desc.Width)*desc.Height*4);
        for (unsigned y=0;y<desc.Height;++y)
          std::copy_n(static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,
                      std::size_t(desc.Width)*4,pixels.data()+std::size_t(y)*desc.Width*4);
        context_->Unmap(staging.Get(),0);
        diagnostics_.recordOverlay(desc.Width,desc.Height,pixels);
      }
    }
  }
  const auto presentResult = swapChain_->Present(1,0);
  if (deviceLost(presentResult)) {
    recoverGraphics(); return;
  }
  if (FAILED(presentResult)) { debugLog("Overlay presentation failed"); return; }
  if (compositionDevice_) {
    const auto commitResult = compositionDevice_->Commit();
    if (FAILED(commitResult)) {
      const auto removed = device_ ? device_->GetDeviceRemovedReason() : commitResult;
      if (deviceLost(removed)) recoverGraphics(); else debugLog("Overlay composition failed");
      return;
    }
  }
  renderMs_ = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderStart).count();
  renderedCacheGeneration_ = cacheGeneration;
  renderedFrameRevision_ = capturedRevision;
  renderedStatusVersion_ = statusVersion_;
  renderedHasPanels_ = !placedPanels.empty();
  forceRender_ = false;
}
LRESULT OverlayWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
  switch (message) {
    case WM_TIMER:
      if (forceRender_ || (mode_ == DisplayMode::Translation &&
          (cache_.generation() != renderedCacheGeneration_ ||
           (capture_ && renderedHasPanels_ && capture_->latestFrame().revision != renderedFrameRevision_) ||
           (showDiagnostics_ && statusVersion_ != renderedStatusVersion_))) ||
          diagnostics_.overlayDue()) render();
      return 0;
    case WM_SIZE: resize(LOWORD(lParam),HIWORD(lParam)); return 0;
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_HOTKEY:
      if (wParam == kToggleTranslation) {
        if (!showDiagnostics_) {
          showDiagnostics_ = true;
          diagnostics_.setEnabled(true);
        }
        setMode(mode_ == DisplayMode::Translation ? DisplayMode::Original : DisplayMode::Translation);
      }
      else if (wParam == kShowOriginal) setMode(DisplayMode::Original);
      else if (wParam == kDiagnostics) setDiagnostics(!showDiagnostics_);
      else if (wParam == kExit) { debugLog("Exit hotkey pressed"); DestroyWindow(hwnd_); }
      return 0;
    case WM_DISPLAYCHANGE: setFullscreenBounds(); render(); return 0;
    case WM_DPICHANGED: SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE); return 0;
    case WM_CLOSE: debugLog("Close requested"); DestroyWindow(hwnd_); return 0;
    case WM_DESTROY: KillTimer(hwnd_,1); PostQuitMessage(0); return 0;
    case WM_ERASEBKGND: return 1;
  } return DefWindowProcW(hwnd_, message, wParam, lParam);
}
LRESULT CALLBACK OverlayWindow::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (message == WM_NCCREATE) { self = static_cast<OverlayWindow*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); self->hwnd_ = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
  if (message == WM_NCDESTROY) {
    SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
    if (self) self->hwnd_ = nullptr;
    return DefWindowProcW(hwnd,message,wParam,lParam);
  }
  return self ? self->handleMessage(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}
} // namespace d4r0
