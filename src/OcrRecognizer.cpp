#include "d4r0/OcrRecognizer.h"
#include <onnxruntime_cxx_api.h>
#include <dml_provider_factory.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace d4r0 {
namespace {
void validateCrop(std::span<const std::uint8_t> bgra, int width, int height) {
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
      bgra.size() != static_cast<std::size_t>(width) * height * 4)
    throw std::invalid_argument("Invalid OCR crop dimensions");
}
std::vector<float> resizeBgr(std::span<const std::uint8_t> bgra, int width, int height,
                             int resizedWidth, int resizedHeight, int tensorWidth,
                             const std::array<float,3>& mean, const std::array<float,3>& deviation) {
  std::vector<float> pixels(3 * resizedHeight * tensorWidth, 0.0F);
  for (int y = 0; y < resizedHeight; ++y) {
    const float sy = std::clamp((y + 0.5F) * height / resizedHeight - 0.5F, 0.0F, float(height - 1));
    const int y0 = int(sy), y1 = std::min(y0 + 1, height - 1);
    for (int x = 0; x < resizedWidth; ++x) {
      const float sx = std::clamp((x + 0.5F) * width / resizedWidth - 0.5F, 0.0F, float(width - 1));
      const int x0 = int(sx), x1 = std::min(x0 + 1, width - 1);
      for (int c = 0; c < 3; ++c) {
        auto sample = [&](int px, int py) { return float(bgra[(std::size_t(py) * width + px) * 4 + c]); };
        float top = std::lerp(sample(x0,y0), sample(x1,y0), sx-x0);
        float bottom = std::lerp(sample(x0,y1), sample(x1,y1), sx-x0);
        pixels[(c * resizedHeight + y) * tensorWidth + x] =
            (std::lerp(top,bottom,sy-y0) / 255.0F - mean[c]) / deviation[c];
      }
    }
  }
  return pixels;
}
Ort::SessionOptions sessionOptions(int adapter) {
  Ort::SessionOptions options;
  options.SetIntraOpNumThreads(4);
  options.DisableMemPattern();
  options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
  if (adapter >= 0) Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_DML(options, adapter));
  return options;
}
}
struct OcrRecognizer::State {
  Ort::Env environment{ORT_LOGGING_LEVEL_ERROR, "d4r0-ocr"};
  Ort::Session session{nullptr};
  std::string input, output;
  std::vector<std::string> characters{ "" }; // CTC blank.
};

OcrRecognizer::OcrRecognizer(const std::filesystem::path& model,
                             const std::filesystem::path& dictionary, int adapter)
    : state_(std::make_unique<State>()) {
  // Read only Paddle's pinned character_dict sequence, not arbitrary YAML.
  std::ifstream file(dictionary);
  if (!file) throw std::runtime_error("Cannot open OCR character dictionary");
  bool inDictionary = false;
  for (std::string line; std::getline(file, line);) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line == "  character_dict:") { inDictionary = true; continue; }
    if (!inDictionary) continue;
    if (!line.starts_with("  - ")) break;
    auto token = line.substr(4);
    if (token.size() >= 2 && token.front() == '\'' && token.back() == '\'') {
      token = token.substr(1, token.size() - 2);
      for (std::size_t pos = 0; (pos = token.find("''", pos)) != std::string::npos; ++pos)
        token.erase(pos, 1);
    } else if (!token.empty() && token.front() == '"') {
      throw std::runtime_error("Unsupported quoted OCR dictionary entry");
    }
    state_->characters.push_back(std::move(token));
  }
  if (state_->characters.size() < 100) throw std::runtime_error("Incomplete OCR dictionary");
  state_->characters.push_back(" ");
  auto options = sessionOptions(adapter);
  state_->session = Ort::Session(state_->environment, model.c_str(), options);
  Ort::AllocatorWithDefaultOptions allocator;
  state_->input = state_->session.GetInputNameAllocated(0, allocator).get();
  state_->output = state_->session.GetOutputNameAllocated(0, allocator).get();
}
OcrRecognizer::~OcrRecognizer() = default;

OcrText OcrRecognizer::recognize(std::span<const std::uint8_t> bgra, int width, int height) {
  validateCrop(bgra, width, height);
  const int resizedWidth = static_cast<int>(std::ceil(48.0 * width / height));
  if (resizedWidth > 3200) throw std::invalid_argument("OCR line is too wide; split into smaller crops");
  const int tensorWidth = std::max(320, resizedWidth);
  auto pixels = resizeBgr(bgra, width, height, resizedWidth, 48, tensorWidth,
                           {0.5F,0.5F,0.5F}, {0.5F,0.5F,0.5F});
  std::array<int64_t,4> shape{1,3,48,tensorWidth};
  auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  auto tensor = Ort::Value::CreateTensor<float>(memory, pixels.data(), pixels.size(), shape.data(), shape.size());
  const char* inputs[]{state_->input.c_str()};
  const char* outputs[]{state_->output.c_str()};
  auto result = state_->session.Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
  const auto dimensions = result.front().GetTensorTypeAndShapeInfo().GetShape();
  if (dimensions.size() != 3 || dimensions[0] != 1 || dimensions[2] != state_->characters.size())
    throw std::runtime_error("OCR output does not match its character dictionary");
  const auto* scores = result.front().GetTensorData<float>();
  OcrText recognized;
  std::size_t previous = 0, count = 0, classes = state_->characters.size();
  for (int64_t t = 0; t < dimensions[1]; ++t) {
    const auto* row = scores + t * classes;
    auto index = std::size_t(std::max_element(row, row + classes) - row);
    if (index != 0 && index != previous) {
      recognized.text += state_->characters[index];
      recognized.confidence += row[index];
      ++count;
    }
    previous = index;
  }
  if (count) recognized.confidence /= float(count);
  return recognized;
}

struct OcrDetector::State {
  Ort::Env environment{ORT_LOGGING_LEVEL_ERROR, "d4r0-detector"};
  Ort::Session session{nullptr};
  std::string input, output;
};
OcrDetector::OcrDetector(const std::filesystem::path& model, int adapter)
    : state_(std::make_unique<State>()) {
  state_->session = Ort::Session(state_->environment, model.c_str(), sessionOptions(adapter));
  Ort::AllocatorWithDefaultOptions allocator;
  state_->input = state_->session.GetInputNameAllocated(0, allocator).get();
  state_->output = state_->session.GetOutputNameAllocated(0, allocator).get();
}
OcrDetector::~OcrDetector() = default;

std::vector<OcrBox> OcrDetector::detect(std::span<const std::uint8_t> bgra, int width, int height,
                                       int longSide, float threshold) {
  validateCrop(bgra, width, height);
  if (longSide < 32 || longSide > 2048 || !std::isfinite(threshold) || threshold <= 0 || threshold >= 1)
    throw std::invalid_argument("Invalid OCR detector settings");
  const float scale = float(longSide) / std::max(width, height);
  const int tw = std::max(32, int(std::round(width * scale / 32)) * 32);
  const int th = std::max(32, int(std::round(height * scale / 32)) * 32);
  auto pixels = resizeBgr(bgra, width, height, tw, th, tw,
                           {0.485F,0.456F,0.406F}, {0.229F,0.224F,0.225F});
  std::array<int64_t,4> shape{1,3,th,tw};
  auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
  auto tensor = Ort::Value::CreateTensor<float>(memory, pixels.data(), pixels.size(), shape.data(), shape.size());
  const char* inputs[]{state_->input.c_str()};
  const char* outputs[]{state_->output.c_str()};
  auto result = state_->session.Run(Ort::RunOptions{nullptr}, inputs, &tensor, 1, outputs, 1);
  const auto dimensions = result.front().GetTensorTypeAndShapeInfo().GetShape();
  if (dimensions != std::vector<int64_t>{1,1,th,tw})
    throw std::runtime_error("Unexpected OCR detector output shape");
  const auto* scores = result.front().GetTensorData<float>();
  std::vector<std::uint8_t> seen(tw * th);
  std::vector<int> pending;
  std::vector<OcrBox> boxes;
  for (int start = 0; start < tw * th; ++start) {
    if (seen[start] || !(scores[start] > threshold)) continue;
    pending.clear(); pending.push_back(start); seen[start] = 1;
    int left = start % tw, right = left, top = start / tw, bottom = top;
    float confidence = 0;
    for (std::size_t i = 0; i < pending.size(); ++i) {
      const int p = pending[i], x = p % tw, y = p / tw;
      left = std::min(left,x); right = std::max(right,x);
      top = std::min(top,y); bottom = std::max(bottom,y);
      confidence += scores[p];
      for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || nx >= tw || ny < 0 || ny >= th) continue;
        const int next = ny * tw + nx;
        if (!seen[next] && scores[next] > threshold) { seen[next] = 1; pending.push_back(next); }
      }
    }
    if (pending.size() < 6 || right-left < 2 || bottom-top < 2) continue;
    confidence /= float(pending.size());
    // ponytail: axis-aligned UI text boxes; rotated text needs polygon unclip/rectification.
    const float w = float(right-left+1), h = float(bottom-top+1);
    const float padding = w * h * 1.5F / (2 * (w+h));
    int x0 = std::clamp(int(std::floor((left-padding)*width/tw)), 0, width);
    int y0 = std::clamp(int(std::floor((top-padding)*height/th)), 0, height);
    int x1 = std::clamp(int(std::ceil((right+1+padding)*width/tw)), 0, width);
    int y1 = std::clamp(int(std::ceil((bottom+1+padding)*height/th)), 0, height);
    boxes.push_back({x0,y0,x1-x0,y1-y0,confidence});
    if (boxes.size() == 1000) break;
  }
  std::sort(boxes.begin(), boxes.end(), [](const auto& a, const auto& b) {
    return a.y == b.y ? a.x < b.x : a.y < b.y;
  });
  return boxes;
}
}
