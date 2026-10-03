#pragma once
#include "d4r0/TranslationProfiles.h"
#include <string>
#include <string_view>
#include <vector>
namespace d4r0 {
std::string makeTranslationPrompt(const std::vector<std::string>& germanBlocks);
std::string makeTranslationPrompt(const std::vector<std::string>& germanBlocks,
                                  const TranslationProfile& profile);
std::string makeSingleTranslationPrompt(std::string_view germanText,
                                        const TranslationProfile& profile);
std::vector<std::string> parseNumberedTranslations(std::string_view output, std::size_t expected);
std::string contextCacheKey(std::string_view source);
bool preservesProtectedTokens(std::string_view source, std::string_view translation);
} // namespace d4r0
