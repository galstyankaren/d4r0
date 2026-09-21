#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace d4r0 {
struct OcrText { std::string text; float confidence{}; };
struct OcrBox { int x{}, y{}, width{}, height{}; float confidence{}; };
class OcrDetector {
 public:
  explicit OcrDetector(const std::filesystem::path& model, int directMlAdapter = -1);
  ~OcrDetector();
  OcrDetector(const OcrDetector&) = delete;
  std::vector<OcrBox> detect(std::span<const std::uint8_t> bgra, int width, int height,
                             int longSide = 960, float threshold = 0.3F);
 private:
  struct State;
  std::unique_ptr<State> state_;
};
// Input is a selected, tightly packed BGRA text-line crop, never a retained screenshot.
class OcrRecognizer {
 public:
  OcrRecognizer(const std::filesystem::path& model, const std::filesystem::path& dictionary,
                int directMlAdapter = -1);
  ~OcrRecognizer();
  OcrRecognizer(const OcrRecognizer&) = delete;
  OcrText recognize(std::span<const std::uint8_t> bgra, int width, int height);
 private:
  struct State;
  std::unique_ptr<State> state_;
};
}
