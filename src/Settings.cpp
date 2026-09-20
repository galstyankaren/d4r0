#include "d4r0/Settings.h"
#include <fstream>
#include <sstream>
namespace d4r0 {
PipelineSettings SettingsStore::load() const {
  PipelineSettings s; std::ifstream in(path_); std::string key, value;
  while (std::getline(in, key, '=') && std::getline(in, value)) {
    try {
      if (key == "model") s.selectedModel = value == "12b" ? ModelChoice::TranslateGemma12B : ModelChoice::TranslateGemma4B;
      else if (key == "stabilityDelayMs") s.stabilityDelayMs = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "ocrCadenceMs") s.ocrCadenceMs = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "minimumOcrConfidence") s.minimumOcrConfidence = std::stof(value);
      else if (key == "llamaExecutable") s.llamaExecutable = std::filesystem::path(value);
      else if (key == "model4b") s.model4b = std::filesystem::path(value);
      else if (key == "model12b") s.model12b = std::filesystem::path(value);
    } catch (const std::exception&) { }
  } return s;
}
bool SettingsStore::save(const PipelineSettings& s) const {
  std::error_code error; std::filesystem::create_directories(path_.parent_path(), error); if (error) return false;
  std::ofstream out(path_); if (!out) return false;
  out << "model=" << (s.selectedModel == ModelChoice::TranslateGemma12B ? "12b" : "4b") << '\n'
      << "stabilityDelayMs=" << s.stabilityDelayMs << '\n' << "ocrCadenceMs=" << s.ocrCadenceMs << '\n'
      << "minimumOcrConfidence=" << s.minimumOcrConfidence << '\n'
      << "llamaExecutable=" << s.llamaExecutable.string() << '\n' << "model4b=" << s.model4b.string() << '\n' << "model12b=" << s.model12b.string() << '\n';
  return static_cast<bool>(out);
}
} // namespace d4r0
