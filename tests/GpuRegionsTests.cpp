#include "d4r0/GpuRegions.h"
#include <winrt/base.h>
#include <cassert>
#include <iostream>
#include <stdexcept>

int main() {
  try {
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr));
    d4r0::GpuRegions regions(device.Get());
    auto makeFrame = [&](unsigned width, unsigned height, const std::vector<std::uint32_t>& pixels) {
      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = width; desc.Height = height; desc.MipLevels = 1; desc.ArraySize = 1;
      desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
      D3D11_SUBRESOURCE_DATA data{pixels.data(), width*4, 0};
      Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
      winrt::check_hresult(device->CreateTexture2D(&desc, &data, &texture));
      return texture;
    };
    std::vector<std::uint32_t> pixels(130*70, 0xFF000000);
    auto black = makeFrame(130,70,pixels);
    assert((regions.compare(black.Get()) == std::vector<std::uint32_t>{4096,4096,128,384,384,12}));
    assert((regions.compare(black.Get()) == std::vector<std::uint32_t>(6,0)));
    pixels[1] = 0xFF0000FF;
    pixels[129+69*130] = 0xFFFFFFFF;
    auto changed = makeFrame(130,70,pixels);
    assert((regions.compare(changed.Get()) == std::vector<std::uint32_t>{1,0,0,0,0,1}));
    auto crop = regions.readCrop(changed.Get(), 128,68,2,2);
    assert(crop.size() == 16 && crop[12] == 255 && crop[13] == 255 && crop[14] == 255);
    assert((regions.compare(changed.Get()) == std::vector<std::uint32_t>(6,0)));
    pixels[2] = 0xFF010101;
    auto noise = makeFrame(130,70,pixels);
    assert((regions.compare(noise.Get(), 0.08F) == std::vector<std::uint32_t>(6,0)));
    auto resized = makeFrame(65,65,std::vector<std::uint32_t>(65*65,0));
    assert((regions.compare(resized.Get()) == std::vector<std::uint32_t>{4096,64,64,1}));
    for (unsigned value = 10; value <= 30; value += 10) {
      auto fade = makeFrame(65,65,std::vector<std::uint32_t>(65*65,0xFF000000 | value*0x010101));
      const auto differences = regions.compare(fade.Get());
      if (value < 30) assert((differences == std::vector<std::uint32_t>(4,0)));
      else assert((differences == std::vector<std::uint32_t>{4096,64,64,1}));
    }
    try { regions.readCrop(changed.Get(),0,0,130,70); return 1; } catch (const std::invalid_argument&) {}
    try { regions.readCrop(changed.Get(),129,69,2,2); return 1; } catch (const std::invalid_argument&) {}
    try { regions.compare(nullptr); return 1; } catch (const std::invalid_argument&) {}
    std::cout << "GPU differences and crop-only readback passed\n";
  } catch (const winrt::hresult_error& error) {
    std::cerr << "GPU test HRESULT " << std::hex << error.code().value << '\n'; return 1;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
