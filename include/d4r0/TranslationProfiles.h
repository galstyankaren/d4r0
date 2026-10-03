#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace d4r0 {
enum class ProfileTemplate { Game, Browser, Professional, General };

struct Language {
  std::string code;
  std::wstring name;
};

struct TranslationProfile {
  std::string id;
  std::wstring displayName;
  std::filesystem::path executablePath;
  ProfileTemplate templateKind{ProfileTemplate::General};
  std::string sourceLanguage{"de"};
  std::string targetLanguage{"en"};
  std::wstring additionalInstructions;
};

struct AppIdentity {
  std::filesystem::path executablePath;
  std::wstring displayName;
  ProfileTemplate suggestedTemplate{ProfileTemplate::General};
};

struct ActiveProfile {
  AppIdentity app;
  TranslationProfile profile;
  bool savedMatch{};
};

struct ProfileDraftRequest {
  std::uint64_t requestId{};
  std::wstring appName;
  std::wstring description;
  ProfileTemplate templateKind{ProfileTemplate::General};
  std::string sourceLanguage{"de"};
  std::string targetLanguage{"en"};
};

struct ProfileDraftResult {
  std::uint64_t requestId{};
  std::wstring additionalInstructions;
  std::wstring error;
};

[[nodiscard]] std::vector<TranslationProfile> defaultProfiles();
[[nodiscard]] ActiveProfile profileForApp(const std::vector<TranslationProfile>& profiles,
                                          const AppIdentity& app);
[[nodiscard]] bool sameExecutablePath(const std::filesystem::path& left,
                                      const std::filesystem::path& right);
[[nodiscard]] std::wstring templateName(ProfileTemplate kind);
[[nodiscard]] const std::vector<Language>& sourceLanguages();
[[nodiscard]] const std::vector<Language>& targetLanguages();
[[nodiscard]] bool isSupportedSourceLanguage(const std::string& code);
[[nodiscard]] bool isSupportedTargetLanguage(const std::string& code);
[[nodiscard]] std::optional<std::string> validateProfile(const TranslationProfile& profile);
} // namespace d4r0
