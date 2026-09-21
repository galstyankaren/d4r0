#include "d4r0/ReplayBuffer.h"
#include <d3d11_4.h>
#include <winrt/base.h>
#include <wrl/client.h>
#include <filesystem>
#include <iostream>

int main() {
  std::filesystem::path directory;
  try {
    const auto stale = std::filesystem::temp_directory_path()/L"d4r0-replay-4294967294";
    std::filesystem::create_directories(stale);
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    winrt::check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT|D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,
        D3D11_SDK_VERSION,&device,nullptr,&context));
    Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
    winrt::check_hresult(context.As(&multithread)); multithread->SetMultithreadProtected(TRUE);
    D3D11_TEXTURE2D_DESC description{};
    description.Width = 640; description.Height = 360; description.MipLevels = 1; description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM; description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT; description.BindFlags = D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(device->CreateTexture2D(&description,nullptr,&texture));
    {
      d4r0::ReplayBuffer replay; directory = replay.directory();
      if (!replay.start()) throw std::runtime_error("Media Foundation replay startup failed");
      if (std::filesystem::exists(stale)) throw std::runtime_error("Dead-process replay was not cleaned on startup");
      const auto start = std::chrono::steady_clock::now();
      replay.submitSourceFrame(texture.Get(),start);
      replay.submitSourceFrame(texture.Get(),start+std::chrono::milliseconds(34));
      for (int segment = 1; segment <= 20; ++segment)
        replay.submitSourceFrame(texture.Get(),start+std::chrono::seconds(31*segment));
      if (!replay.active()) {
        std::wcerr << L"Hardware H.264 replay encoder rejected GPU frames: " << replay.lastError() << L'\n';
        return 1;
      }
      replay.stop();
      std::size_t files{}, bytes{};
      for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == L".mp4") { ++files; bytes += std::size_t(entry.file_size()); }
      }
      if (files != 20 || !bytes) throw std::runtime_error("Replay segment ring did not retain exactly ten minutes");
    }
    if (std::filesystem::exists(directory)) throw std::runtime_error("Replay directory survived object exit");
    std::cout << "GPU-source hardware replay and exit cleanup passed\n";
    return 0;
  } catch (const winrt::hresult_error& error) {
    std::cerr << "Replay HRESULT 0x" << std::hex << std::uint32_t(error.code().value) << '\n';
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
  return 1;
}
