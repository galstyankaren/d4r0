#include "d4r0/GpuRegions.h"
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace d4r0 {
using Microsoft::WRL::ComPtr;
namespace {
constexpr char source[] = R"(
Texture2D<float4> currentFrame : register(t0);
Texture2D<float4> previousFrame : register(t1);
RWStructuredBuffer<uint> changes : register(u0);
RWTexture2D<float4> accumulatedFrame : register(u1);
cbuffer Parameters : register(b0) { uint width; uint height; uint columns; float threshold; };
groupshared uint changed;
[numthreads(16,16,1)]
void main(uint3 group : SV_GroupID, uint3 thread : SV_GroupThreadID, uint index : SV_GroupIndex) {
  if (index == 0) changed = 0;
  GroupMemoryBarrierWithGroupSync();
  uint count = 0;
  for (uint y = thread.y; y < 64; y += 16) {
    for (uint x = thread.x; x < 64; x += 16) {
      uint2 p = group.xy * 64 + uint2(x,y);
      if (p.x < width && p.y < height) {
        float4 current = currentFrame.Load(int3(p,0));
        float4 previous = previousFrame.Load(int3(p,0));
        float3 delta = abs(current.rgb - previous.rgb);
        bool different = max(delta.r,max(delta.g,delta.b)) > threshold;
        if (different) ++count;
        accumulatedFrame[p] = different ? current : previous;
      }
    }
  }
  InterlockedAdd(changed, count);
  GroupMemoryBarrierWithGroupSync();
  if (index == 0) changes[group.y * columns + group.x] = changed;
}
)";
D3D11_TEXTURE2D_DESC describe(ID3D11Texture2D* frame, ID3D11Device* expected) {
  if (!frame) throw std::invalid_argument("Missing GPU frame");
  ComPtr<ID3D11Device> device;
  frame->GetDevice(&device);
  D3D11_TEXTURE2D_DESC description{};
  frame->GetDesc(&description);
  if (device.Get() != expected || description.Format != DXGI_FORMAT_B8G8R8A8_UNORM ||
      description.SampleDesc.Count != 1 || description.ArraySize != 1 || description.MipLevels != 1)
    throw std::invalid_argument("Unsupported GPU frame format or device");
  return description;
}
}
GpuRegions::GpuRegions(ID3D11Device* device) : device_(device) {
  if (!device) throw std::invalid_argument("Missing GPU device");
  device->GetImmediateContext(&context_);
  ComPtr<ID3DBlob> bytecode, errors;
  winrt::check_hresult(D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr,
      "main", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &bytecode, &errors));
  winrt::check_hresult(device->CreateComputeShader(bytecode->GetBufferPointer(), bytecode->GetBufferSize(), nullptr, &shader_));
  D3D11_BUFFER_DESC description{};
  description.ByteWidth = 16; description.Usage = D3D11_USAGE_DEFAULT;
  description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  winrt::check_hresult(device->CreateBuffer(&description, nullptr, &parameters_));
}

std::vector<std::uint32_t> GpuRegions::compare(ID3D11Texture2D* frame, float threshold) {
  const auto description = describe(frame, device_.Get());
  if (!std::isfinite(threshold) || threshold < 0 || threshold > 1)
    throw std::invalid_argument("Invalid GPU difference threshold");
  const unsigned columns = (description.Width+tileSize-1)/tileSize;
  const unsigned rows = (description.Height+tileSize-1)/tileSize;
  std::vector<std::uint32_t> changes(columns * rows);
  ComPtr<ID3D11Texture2D> baseline = frame;
  D3D11_TEXTURE2D_DESC old{};
  if (previous_) previous_->GetDesc(&old);
  if (!previous_ || old.Width != description.Width || old.Height != description.Height) {
    for (unsigned y = 0; y < rows; ++y) for (unsigned x = 0; x < columns; ++x)
      changes[y*columns+x] = std::min(tileSize, description.Width-x*tileSize) *
                             std::min(tileSize, description.Height-y*tileSize);
  } else {
    ComPtr<ID3D11ShaderResourceView> currentView, previousView;
    winrt::check_hresult(device_->CreateShaderResourceView(frame, nullptr, &currentView));
    winrt::check_hresult(device_->CreateShaderResourceView(previous_.Get(), nullptr, &previousView));
    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = unsigned(changes.size()*sizeof(std::uint32_t));
    buffer.Usage = D3D11_USAGE_DEFAULT; buffer.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    buffer.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    buffer.StructureByteStride = sizeof(std::uint32_t);
    ComPtr<ID3D11Buffer> output, staging;
    winrt::check_hresult(device_->CreateBuffer(&buffer, nullptr, &output));
    ComPtr<ID3D11UnorderedAccessView> outputView;
    winrt::check_hresult(device_->CreateUnorderedAccessView(output.Get(), nullptr, &outputView));
    auto baselineDescription = description;
    baselineDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    baselineDescription.Usage = D3D11_USAGE_DEFAULT;
    baselineDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    baselineDescription.MiscFlags = 0; baselineDescription.CPUAccessFlags = 0;
    baseline.Reset();
    winrt::check_hresult(device_->CreateTexture2D(&baselineDescription, nullptr, &baseline));
    ComPtr<ID3D11UnorderedAccessView> baselineView;
    winrt::check_hresult(device_->CreateUnorderedAccessView(baseline.Get(), nullptr, &baselineView));
    buffer.Usage = D3D11_USAGE_STAGING; buffer.BindFlags = 0;
    buffer.MiscFlags = 0; buffer.StructureByteStride = 0; buffer.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    winrt::check_hresult(device_->CreateBuffer(&buffer, nullptr, &staging));
    struct { unsigned width, height, columns; float threshold; } parameters{
        description.Width, description.Height, columns, threshold};
    context_->UpdateSubresource(parameters_.Get(), 0, nullptr, &parameters, 0, 0);
    ID3D11ShaderResourceView* views[]{currentView.Get(),previousView.Get()};
    context_->CSSetShader(shader_.Get(), nullptr, 0);
    context_->CSSetConstantBuffers(0, 1, parameters_.GetAddressOf());
    context_->CSSetShaderResources(0, 2, views);
    ID3D11UnorderedAccessView* outputViews[]{outputView.Get(),baselineView.Get()};
    context_->CSSetUnorderedAccessViews(0, 2, outputViews, nullptr);
    context_->Dispatch(columns, rows, 1);
    ID3D11ShaderResourceView* noViews[2]{};
    ID3D11UnorderedAccessView* noOutput[2]{};
    context_->CSSetShaderResources(0, 2, noViews);
    context_->CSSetUnorderedAccessViews(0, 2, noOutput, nullptr);
    context_->CopyResource(staging.Get(), output.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
    std::memcpy(changes.data(), mapped.pData, changes.size()*sizeof(std::uint32_t));
    context_->Unmap(staging.Get(), 0);
  }
  // Keep each pixel's reference until accumulated change crosses the threshold.
  // Otherwise a slow fade would look unchanged forever.
  previous_ = std::move(baseline);
  return changes;
}

std::vector<std::uint8_t> GpuRegions::readCrop(ID3D11Texture2D* frame, unsigned x, unsigned y,
                                             unsigned width, unsigned height) {
  auto description = describe(frame, device_.Get());
  if (!width || !height || x >= description.Width || y >= description.Height ||
      width > description.Width-x || height > description.Height-y ||
      width > 2048 || height > 2048 || (width == description.Width && height == description.Height))
    throw std::invalid_argument("OCR readback must be a bounded subregion, not a full frame");
  description.Width = width; description.Height = height;
  description.Usage = D3D11_USAGE_STAGING; description.BindFlags = 0;
  description.MiscFlags = 0; description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Texture2D> staging;
  winrt::check_hresult(device_->CreateTexture2D(&description, nullptr, &staging));
  D3D11_BOX box{x,y,0,x+width,y+height,1};
  context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, frame, 0, &box);
  std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  winrt::check_hresult(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped));
  for (unsigned row = 0; row < height; ++row)
    std::memcpy(pixels.data()+std::size_t(row)*width*4,
        static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(row)*mapped.RowPitch, width*4);
  context_->Unmap(staging.Get(), 0);
  return pixels;
}
}
