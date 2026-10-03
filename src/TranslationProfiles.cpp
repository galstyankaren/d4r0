#include "d4r0/TranslationProfiles.h"

#include <algorithm>
#include <cwctype>

namespace d4r0 {
namespace {
std::wstring folded(std::wstring value) {
  std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) { return std::towlower(c); });
  return value;
}

} // namespace

bool sameExecutablePath(const std::filesystem::path& left, const std::filesystem::path& right) {
  if (left.empty() || right.empty()) return false;
  return folded(left.lexically_normal().wstring()) == folded(right.lexically_normal().wstring());
}

std::vector<TranslationProfile> defaultProfiles() {
  return {
    {"template-game", L"Game", {}, ProfileTemplate::Game, "de", "en",
     L"Translate dialogue naturally. Keep character voice, quest terms, item names, and UI labels consistent."},
    {"template-browser", L"Browser", {}, ProfileTemplate::Browser, "de", "en",
     L"Translate page text naturally. Preserve headings, links, names, and the original tone."},
    {"template-professional", L"Professional", {}, ProfileTemplate::Professional, "de", "en",
     L"Use clear, accurate professional language. Preserve technical terms, names, and formatting."},
    {"template-general", L"General", {}, ProfileTemplate::General, "de", "en", L""}
  };
}

ActiveProfile profileForApp(const std::vector<TranslationProfile>& profiles, const AppIdentity& app) {
  for (const auto& profile : profiles) {
    if (!profile.executablePath.empty() && sameExecutablePath(profile.executablePath, app.executablePath))
      return {app, profile, true};
  }

  for (const auto& profile : profiles) {
    if (profile.executablePath.empty() && profile.templateKind == app.suggestedTemplate)
      return {app, profile, false};
  }
  for (const auto& profile : profiles) {
    if (profile.executablePath.empty() && profile.templateKind == ProfileTemplate::General)
      return {app, profile, false};
  }

  const auto defaults = defaultProfiles();
  const auto found = std::find_if(defaults.begin(), defaults.end(), [](const auto& profile) {
    return profile.templateKind == ProfileTemplate::General;
  });
  return {app, *found, false};
}

std::wstring templateName(ProfileTemplate kind) {
  switch (kind) {
    case ProfileTemplate::Game: return L"Game";
    case ProfileTemplate::Browser: return L"Browser";
    case ProfileTemplate::Professional: return L"Professional";
    case ProfileTemplate::General: return L"General";
  }
  return L"General";
}

const std::vector<Language>& targetLanguages() {
  static const std::vector<Language> languages = {
    {"af", L"Afrikaans"}, {"am", L"Amharic"}, {"ar", L"Arabic"}, {"bg", L"Bulgarian"},
    {"bn", L"Bengali"}, {"ca", L"Catalan"}, {"cs", L"Czech"}, {"da", L"Danish"},
    {"de", L"German"}, {"el", L"Greek"}, {"en", L"English"}, {"es", L"Spanish"},
    {"et", L"Estonian"}, {"fa", L"Persian"}, {"fi", L"Finnish"}, {"fr", L"French"},
    {"gu", L"Gujarati"}, {"ha", L"Hausa"}, {"hi", L"Hindi"}, {"hr", L"Croatian"},
    {"hu", L"Hungarian"}, {"id", L"Indonesian"}, {"it", L"Italian"}, {"ja", L"Japanese"},
    {"kn", L"Kannada"}, {"ko", L"Korean"}, {"lt", L"Lithuanian"}, {"lv", L"Latvian"},
    {"ml", L"Malayalam"}, {"mr", L"Marathi"}, {"ms", L"Malay"}, {"mt", L"Maltese"},
    {"ne", L"Nepali"}, {"nl", L"Dutch"}, {"no", L"Norwegian"}, {"pa", L"Punjabi"},
    {"pl", L"Polish"}, {"pt", L"Portuguese"}, {"ro", L"Romanian"}, {"ru", L"Russian"},
    {"si", L"Sinhala"}, {"sk", L"Slovak"}, {"sl", L"Slovenian"}, {"sr", L"Serbian"},
    {"sv", L"Swedish"}, {"sw", L"Swahili"}, {"ta", L"Tamil"}, {"te", L"Telugu"},
    {"th", L"Thai"}, {"tr", L"Turkish"}, {"uk", L"Ukrainian"}, {"ur", L"Urdu"},
    {"vi", L"Vietnamese"}, {"yi", L"Yiddish"}, {"zh", L"Chinese"}
  };
  return languages;
}

const std::vector<Language>& sourceLanguages() {
  // PP-OCR Latin recognizer languages intersected with TranslateGemma's supported 55.
  static const std::vector<Language> languages = {
    {"af", L"Afrikaans"}, {"cs", L"Czech"}, {"da", L"Danish"},
    {"de", L"German"}, {"en", L"English"}, {"es", L"Spanish"}, {"et", L"Estonian"},
    {"fr", L"French"}, {"hr", L"Croatian"}, {"hu", L"Hungarian"},
    {"id", L"Indonesian"}, {"it", L"Italian"}, {"lt", L"Lithuanian"},
    {"ms", L"Malay"}, {"nl", L"Dutch"}, {"no", L"Norwegian"},
    {"pl", L"Polish"}, {"pt", L"Portuguese"}, {"sk", L"Slovak"},
    {"sl", L"Slovenian"}, {"sr", L"Serbian (Latin)"}, {"sv", L"Swedish"},
    {"sw", L"Swahili"}, {"tr", L"Turkish"}
  };
  return languages;
}

bool isSupportedSourceLanguage(const std::string& code) {
  const auto& languages = sourceLanguages();
  return std::any_of(languages.begin(), languages.end(), [&](const auto& language) { return language.code == code; });
}

bool isSupportedTargetLanguage(const std::string& code) {
  const auto& languages = targetLanguages();
  return std::any_of(languages.begin(), languages.end(), [&](const auto& language) { return language.code == code; });
}

std::optional<std::string> validateProfile(const TranslationProfile& profile) {
  if (profile.id.empty()) return "profile id is required";
  if (profile.displayName.empty()) return "profile display name is required";
  if (!isSupportedSourceLanguage(profile.sourceLanguage)) return "source language is not supported by Latin OCR and TranslateGemma";
  if (!isSupportedTargetLanguage(profile.targetLanguage)) return "target language is not supported by TranslateGemma";
  if (profile.sourceLanguage == profile.targetLanguage) return "source and target languages must differ";
  if (profile.additionalInstructions.size() > 2048) return "additional instructions must be at most 2048 characters";
  if (profile.additionalInstructions.find(L"<start_of_turn>") != std::wstring::npos ||
      profile.additionalInstructions.find(L"<end_of_turn>") != std::wstring::npos ||
      profile.additionalInstructions.find(L"<start_of_task>") != std::wstring::npos ||
      profile.additionalInstructions.find(L"<end_of_task>") != std::wstring::npos ||
      profile.additionalInstructions.find(L"<bos>") != std::wstring::npos ||
      profile.additionalInstructions.find(L"<eos>") != std::wstring::npos ||
      profile.additionalInstructions.find(L"[BLOCK ") != std::wstring::npos ||
      profile.additionalInstructions.find(L"[/BLOCK ") != std::wstring::npos ||
      profile.additionalInstructions.find(L"[TEXT]") != std::wstring::npos ||
      profile.additionalInstructions.find(L"[/TEXT]") != std::wstring::npos)
    return "additional instructions contain a reserved TranslateGemma delimiter";
  return std::nullopt;
}
} // namespace d4r0
