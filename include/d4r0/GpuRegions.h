#pragma once
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>
#include <span>
#include <vector>

namespace d4r0 {
// Only per-tile change counts cross to CPU. Source pixels remain on the GPU
// until a caller explicitly selects a bounded OCR crop.
class GpuRegions {
 public:
  static constexpr unsigned tileSize = 64;
  explicit GpuRegions(ID3D11Device* device);
  std::vector<std::uint32_t> compare(ID3D11Texture2D* frame, float pixelThreshold = 0.08F);
  std::vector<std::uint8_t> readCrop(ID3D11Texture2D* frame, unsigned x, unsigned y,
                                    unsigned width, unsigned height);
 private:
  Microsoft::WRL::ComPtr<ID3D11Device> device_;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
  Microsoft::WRL::ComPtr<ID3D11ComputeShader> shader_;
  Microsoft::WRL::ComPtr<ID3D11Buffer> parameters_;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> previous_;
};
}
