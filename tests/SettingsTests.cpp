#include "d4r0/Settings.h"
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {
void roundTripsPipelineSettings() {
  const auto path = std::filesystem::current_path() / "d4r0-settings-tests.ini";
  d4r0::PipelineSettings saved;
  assert(saved.detectorThreshold == 0.3F);
  assert(saved.maxHeightRatio == 1.4F);
  assert(saved.maxVerticalGapRatio == 0.8F);
  assert(saved.maxGroupLines == 8);
  saved.detectorThreshold = 0.45F;
  saved.maxHeightRatio = 1.8F;
  saved.maxVerticalGapRatio = 1.1F;
  saved.maxGroupLines = 5;

  const d4r0::SettingsStore store(path);
  assert(store.save(saved));
  const auto loaded = store.load();
  assert(loaded.detectorThreshold == saved.detectorThreshold);
  assert(loaded.maxHeightRatio == saved.maxHeightRatio);
  assert(loaded.maxVerticalGapRatio == saved.maxVerticalGapRatio);
  assert(loaded.maxGroupLines == saved.maxGroupLines);
  std::filesystem::remove(path);
}

void validatesSettings() {
  const auto directory = std::filesystem::current_path() / "d4r0-settings-validation";
  std::filesystem::create_directory(directory);
  const auto touch = [&](const char* name) {
    const auto path = directory / name;
    std::ofstream(path).put('\n');
    return path;
  };
  d4r0::PipelineSettings settings;
  settings.llamaExecutable = touch("llama.exe");
  settings.model4b = touch("model4b.gguf");
  settings.ocrDetector = touch("detector.onnx");
  settings.ocrRecognizer = touch("recognizer.onnx");
  settings.ocrDictionary = touch("dictionary.txt");
  assert(!d4r0::validateSettings(settings));

  const auto invalid = [&](auto change) {
    auto candidate = settings;
    change(candidate);
    assert(d4r0::validateSettings(candidate));
  };
  invalid([](auto& value) { value.stabilityDelayMs = 0; });
  invalid([](auto& value) { value.ocrCadenceMs = 19; });
  invalid([](auto& value) { value.ocrCadenceMs = 2001; });
  invalid([](auto& value) { value.detectorLongSide = 31; });
  invalid([](auto& value) { value.detectorLongSide = 2049; });
  invalid([](auto& value) { value.maxOcrBatch = 0; });
  invalid([](auto& value) { value.maxOcrBatch = 33; });
  invalid([](auto& value) { value.maxTranslationBatch = 0; });
  invalid([](auto& value) { value.maxTranslationBatch = 17; });
  invalid([](auto& value) { value.detectorThreshold = -0.1F; });
  invalid([](auto& value) { value.minimumOcrConfidence = 1.1F; });
  invalid([](auto& value) { value.lowConfidenceOpacity = NAN; });
  invalid([](auto& value) { value.maxHeightRatio = 0.99F; });
  invalid([](auto& value) { value.maxVerticalGapRatio = -0.01F; });
  invalid([](auto& value) { value.maxGroupLines = 0; });
  invalid([](auto& value) { value.ocrAdapter = -2; });
  invalid([](auto& value) { value.toggleShortcut = L"Tab"; });
  invalid([](auto& value) { value.originalShortcut = value.toggleShortcut; });
  invalid([](auto& value) { value.llamaExecutable.clear(); });
  invalid([](auto& value) { value.ocrDetector.clear(); });
  invalid([](auto& value) { value.selectedModel = static_cast<d4r0::ModelChoice>(99); });
  invalid([](auto& value) { value.selectedModel = d4r0::ModelChoice::TranslateGemma12B; });

  settings.selectedModel = d4r0::ModelChoice::TranslateGemma12B;
  settings.model12b = touch("model12b.gguf");
  assert(!d4r0::validateSettings(settings));
  std::filesystem::remove_all(directory);
}
}

int main() {
  roundTripsPipelineSettings();
  validatesSettings();
}
