#include "d4r0/WindowsGraphicsCapture.h"
#include "d4r0/GpuRegions.h"
#include <winrt/base.h>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <atomic>

namespace {
LRESULT CALLBACK paintSource(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{};
    const auto dc = BeginPaint(window,&paint);
    const auto brush = CreateSolidBrush(static_cast<COLORREF>(GetWindowLongPtrW(window,GWLP_USERDATA)));
    FillRect(dc,&paint.rcPaint,brush);
    DeleteObject(brush);
    EndPaint(window,&paint);
    return 0;
  }
  return DefWindowProcW(window,message,wparam,lparam);
}
void pump() {
  MSG message{};
  while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
    TranslateMessage(&message); DispatchMessageW(&message);
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
}
struct TestWindows {
  HWND source{}, overlay{};
  ~TestWindows() { if (overlay) DestroyWindow(overlay); if (source) DestroyWindow(source); }
};
bool mostlyColor(const std::vector<std::uint8_t>& pixels, unsigned b, unsigned g, unsigned r) {
  unsigned matched = 0;
  for (std::size_t i = 0; i < pixels.size(); i += 4)
    if (pixels[i] == b && pixels[i+1] == g && pixels[i+2] == r) ++matched;
  return matched > pixels.size()/4 * 0.9;
}
void verifySnapshotsAndExclusion() {
  RECT monitor{};
  EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR, HDC, LPRECT rect, LPARAM data)->BOOL {
    *reinterpret_cast<RECT*>(data) = *rect; return FALSE;
  },reinterpret_cast<LPARAM>(&monitor));
  WNDCLASSW type{};
  type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"d4r0CaptureTest";
  type.lpfnWndProc = paintSource;
  if (!RegisterClassW(&type)) throw std::runtime_error("Cannot register capture test window");
  TestWindows windows;
  auto create = [&](COLORREF color) {
    auto window = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        type.lpszClassName,L"d4r0 capture test",WS_POPUP,monitor.left+32,monitor.top+32,
        192,192,nullptr,nullptr,type.hInstance,nullptr);
    if (!window) throw std::runtime_error("Cannot create capture test window");
    SetWindowLongPtrW(window,GWLP_USERDATA,color);
    return window;
  };
  windows.source = create(RGB(255,0,0));
  ShowWindow(windows.source,SW_SHOWNOACTIVATE); UpdateWindow(windows.source);
  d4r0::WindowsGraphicsCapture capture(0);
  if (!capture.start()) throw std::runtime_error("Cannot start capture fixture");
  d4r0::CapturedFrame held;
  std::unique_ptr<d4r0::GpuRegions> gpu;
  auto waitColor = [&](unsigned b, unsigned g, unsigned r) {
    const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
      pump();
      auto frame = capture.latestFrame();
      if (!frame.texture) continue;
      if (!gpu) {
        Microsoft::WRL::ComPtr<ID3D11Device> device;
        frame.texture->GetDevice(&device);
        gpu = std::make_unique<d4r0::GpuRegions>(device.Get());
      }
      if (mostlyColor(gpu->readCrop(frame.texture.Get(),48,48,160,160),b,g,r)) return frame;
    }
    throw std::runtime_error("Capture fixture color did not arrive");
  };
  held = waitColor(0,0,255);
  const auto heldPixels = gpu->readCrop(held.texture.Get(),48,48,160,160);
  SetWindowLongPtrW(windows.source,GWLP_USERDATA,RGB(0,255,0));
  InvalidateRect(windows.source,nullptr,FALSE); UpdateWindow(windows.source);
  waitColor(0,255,0);
  SetWindowLongPtrW(windows.source,GWLP_USERDATA,RGB(0,0,255));
  InvalidateRect(windows.source,nullptr,FALSE); UpdateWindow(windows.source);
  waitColor(255,0,0);
  if (gpu->readCrop(held.texture.Get(),48,48,160,160) != heldPixels)
    throw std::runtime_error("Held snapshot changed when the capture pool advanced");
  windows.overlay = create(RGB(255,0,0));
  if (!SetWindowDisplayAffinity(windows.overlay, WDA_EXCLUDEFROMCAPTURE))
    throw std::runtime_error("Cannot exclude capture test overlay");
  ShowWindow(windows.overlay,SW_SHOWNOACTIVATE); UpdateWindow(windows.overlay);
  // Change the source underneath the excluded overlay so a stale frame cannot pass.
  SetWindowLongPtrW(windows.source,GWLP_USERDATA,RGB(0,255,0));
  InvalidateRect(windows.source,nullptr,FALSE); UpdateWindow(windows.source);
  waitColor(0,255,0);
  std::atomic<bool> reading{true};
  std::jthread reader([&] { while (reading.load()) { (void)capture.latestFrame(); std::this_thread::yield(); } });
  capture.stop();
  reading = false;
  reader.join();
  if (gpu->readCrop(held.texture.Get(),48,48,160,160) != heldPixels)
    throw std::runtime_error("Held snapshot changed during stop");
}
}

int main() {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  int result = 0;
  {
    try { verifySnapshotsAndExclusion(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    catch (const winrt::hresult_error& error) { std::cerr << "Capture HRESULT " << std::hex << error.code().value << '\n'; result = 1; }
    d4r0::WindowsGraphicsCapture capture(0);
    for (int iteration = 0; iteration < 3; ++iteration) {
      if (!capture.start()) { result = 1; break; }
      d4r0::CapturedFrame held;
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!held.texture && std::chrono::steady_clock::now() < deadline) {
        held = capture.latestFrame();
        pump();
      }
      if (!held.texture) { result = 1; break; }
      // Retain a snapshot across stop. No source pixels are read or persisted.
      capture.stop();
      if (capture.latestFrame().texture) { result = 1; break; }
      D3D11_TEXTURE2D_DESC description{};
      held.texture->GetDesc(&description);
      if (!description.Width || !description.Height || description.CPUAccessFlags != 0 ||
          !(description.BindFlags & D3D11_BIND_SHADER_RESOURCE)) { result = 1; break; }
      capture.stop();
    }
  }
  winrt::uninit_apartment();
  std::cout << (result ? "Capture lifecycle failed\n" : "Capture lifecycle passed\n");
  return result;
}
