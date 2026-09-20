#include "d4r0/WindowsGraphicsCapture.h"
#include "d4r0/DebugLog.h"

#include <d3d11.h>
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
  winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice d3dDevice{nullptr};
  winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool framePool{nullptr};
  winrt::Windows::Graphics::Capture::GraphicsCaptureSession session{nullptr};
  winrt::Windows::Graphics::SizeInt32 size{};
  winrt::event_token frameArrived{};
};

WindowsGraphicsCapture::~WindowsGraphicsCapture() { stop(); }

bool WindowsGraphicsCapture::start() {
  std::scoped_lock lock(mutex_);
  if (state_) return true;

  MonitorSearch search{.wanted = monitorIndex_};
  EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&search));
  if (!search.monitor) { debugLog("Configured monitor was not found"); return false; }

  auto state = std::make_unique<State>();
  try {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                            D3D11_SDK_VERSION, device_.GetAddressOf(), nullptr, nullptr));
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(device_.As(&dxgiDevice));
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
    state->frameArrived = state->framePool.FrameArrived([this](auto const&, auto const&) { onFrameArrived(); });
    state->session = state->framePool.CreateCaptureSession(item);
    state->session.StartCapture();
  } catch (...) {
    state.reset();
    device_.Reset();
    debugLog("Capture initialization raised an exception");
    return false;
  }
  state_ = state.release();
  debugLog("Windows Graphics Capture started");
  return true;
}

void WindowsGraphicsCapture::stop() {
  std::scoped_lock lock(mutex_);
  if (!state_) return;
  state_->framePool.FrameArrived(state_->frameArrived);
  state_->session.Close();
  state_->framePool.Close();
  delete state_;
  state_ = nullptr;
  latestFrame_ = {};
  device_.Reset();
  debugLog("Windows Graphics Capture stopped");
}

CapturedFrame WindowsGraphicsCapture::latestFrame() const {
  std::scoped_lock lock(mutex_);
  return latestFrame_;
}

void WindowsGraphicsCapture::onFrameArrived() {
  std::scoped_lock lock(mutex_);
  if (!state_) return;
  const auto frame = state_->framePool.TryGetNextFrame();
  if (!frame) return;
  try {
    const auto size = frame.ContentSize();
    if (size.Width != state_->size.Width || size.Height != state_->size.Height) {
      state_->framePool.Recreate(state_->d3dDevice,
          winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 2, size);
      state_->size = size;
      debugLog("Capture frame pool resized");
      return;
    }
    const auto access = frame.Surface().as<
        ::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.GetAddressOf())));
    latestFrame_.texture = std::move(texture);
    ++latestFrame_.revision;
    if (latestFrame_.revision == 1 || latestFrame_.revision % 300 == 0) debugLog("Captured GPU frame");
  } catch (...) {
    debugLog("Dropped an invalid capture frame");
  }
}
}  // namespace d4r0
