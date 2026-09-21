#include "d4r0/WindowsGraphicsCapture.h"
#include "d4r0/DebugLog.h"

#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <memory>
#include <utility>

namespace d4r0 {
namespace {
struct MonitorSearch {
  std::uint32_t wanted{};
  std::uint32_t current{};
  HMONITOR monitor{};
};

BOOL CALLBACK findMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
  auto& search = *reinterpret_cast<MonitorSearch*>(parameter);
  if (search.current++ == search.wanted) {
    search.monitor = monitor;
    return FALSE;
  }
  return TRUE;
}
}  // namespace

struct WindowsGraphicsCapture::State {
  std::mutex mutex;
  bool stopped{};
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  CapturedFrame latest;
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice d3dDevice{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
  winrt::Windows::Graphics::SizeInt32 size{};
  winrt::event_token frameArrived{};
  void onFrameArrived();
  void close() {
    {
      std::scoped_lock lock(mutex);
      stopped = true;
      latest = {};
    }
    // Close outside the callback lock: Close may wait for an in-flight callback.
    try { if (framePool) framePool.FrameArrived(frameArrived); } catch (...) {}
    try { if (session) session.Close(); } catch (...) {}
    try { if (framePool) framePool.Close(); } catch (...) {}
  }
};

WindowsGraphicsCapture::~WindowsGraphicsCapture() { stop(); }

bool WindowsGraphicsCapture::start() {
  std::scoped_lock lock(mutex_);
  if (state_) return true;

  MonitorSearch search{.wanted = monitorIndex_};
  EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&search));
  if (!search.monitor) { debugLog("Configured monitor was not found"); return false; }

  auto state = std::make_shared<State>();
  try {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                            D3D11_SDK_VERSION, state->device.GetAddressOf(), nullptr, state->context.GetAddressOf()));
    Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
    winrt::check_hresult(state->context.As(&multithread));
    multithread->SetMultithreadProtected(TRUE);
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(state->device.As(&dxgiDevice));
    winrt::com_ptr<IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put()));
    state->d3dDevice = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

    auto factory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem,
                                                 IGraphicsCaptureItemInterop>();
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(factory->CreateForMonitor(search.monitor, winrt::guid_of<decltype(item)>(), winrt::put_abi(item)));
    state->size = item.Size();
    if (state->size.Width <= 0 || state->size.Height <= 0) throw winrt::hresult_invalid_argument();

    state->framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
        state->d3dDevice, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, state->size);
    state->frameArrived = state->framePool.FrameArrived([weak = std::weak_ptr<State>(state)](auto const&, auto const&) {
      if (const auto active = weak.lock()) active->onFrameArrived();
    });
    state->session = state->framePool.CreateCaptureSession(item);
    state->session.StartCapture();
  } catch (...) {
    state->close();
    debugLog("Capture initialization raised an exception");
    return false;
  }
  state_ = std::move(state);
  debugLog("Windows Graphics Capture started");
  return true;
}

void WindowsGraphicsCapture::stop() {
  std::shared_ptr<State> state;
  {
    std::scoped_lock lock(mutex_);
    state = std::exchange(state_, {});
  }
  if (!state) return;
  state->close();
  debugLog("Windows Graphics Capture stopped");
}

CapturedFrame WindowsGraphicsCapture::latestFrame() const {
  std::scoped_lock lock(mutex_);
  if (!state_) return {};
  std::scoped_lock stateLock(state_->mutex);
  return state_->latest;
}

void WindowsGraphicsCapture::State::onFrameArrived() {
  std::scoped_lock lock(mutex);
  if (stopped) return;
  try {
    auto frame = framePool.TryGetNextFrame();
    if (!frame) return;
    const auto contentSize = frame.ContentSize();
    if (contentSize.Width <= 0 || contentSize.Height <= 0) return;
    if (contentSize.Width != size.Width || contentSize.Height != size.Height) {
      frame.Close();
      frame = nullptr;
      latest.texture.Reset();
      ++latest.revision; // Explicit discontinuity invalidates every pre-resize job.
      framePool.Recreate(d3dDevice,
          winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, contentSize);
      size = contentSize;
      debugLog("Capture frame pool resized");
      return;
    }
    const auto access = frame.Surface().as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.GetAddressOf())));
    D3D11_TEXTURE2D_DESC description{};
    texture->GetDesc(&description);
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    description.CPUAccessFlags = 0;
    description.MiscFlags = 0;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> owned;
    winrt::check_hresult(device->CreateTexture2D(&description, nullptr, &owned));
    context->CopyResource(owned.Get(), texture.Get());
    // Immutable GPU snapshot; the pool may reuse its source as soon as this frame closes.
    latest.texture = std::move(owned);
    ++latest.revision;
    if (latest.revision == 1 || latest.revision % 300 == 0) debugLog("Captured GPU frame");
  } catch (...) {
    debugLog("Dropped an invalid capture frame");
  }
}
}  // namespace d4r0
