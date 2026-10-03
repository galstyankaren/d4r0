#include "d4r0/TranslationPrompt.h"
#include <cassert>

int main() {
  const auto prompt = d4r0::makeTranslationPrompt({"Ziele:\n1. Finde den Schl\xC3\xBCssel", "Leben: 42"});
  assert(prompt.find("[BLOCK 1]\nZiele:\n1. Finde den Schl\xC3\xBCssel\n[/BLOCK 1]") != std::string::npos);

  auto profile = d4r0::defaultProfiles().front();
  profile.sourceLanguage = "de";
  profile.targetLanguage = "fr";
  profile.additionalInstructions = L"Keep item names in French.\nUse concise dialogue.";
  const auto single = d4r0::makeSingleTranslationPrompt("Gib {key} 42", profile);
  assert(single.find("Translate from de to fr") != std::string::npos);
  assert(single.find("Gib {key} 42") != std::string::npos);
  assert(single.find("{key}") != std::string::npos);
  assert(single.find("Keep item names in French.\nUse concise dialogue.") != std::string::npos);
  assert(single.find("<start_of_turn>") == std::string::npos);

  const auto batch = d4r0::makeTranslationPrompt({"Erste Zeile\nZweite Zeile", "Leben: 42"}, profile);
  assert(batch.find("Preserve placeholders, numbers, keyboard shortcuts, markup, line breaks, names, and tokens exactly.") != std::string::npos);
  assert(batch.find("[BLOCK 1]\nErste Zeile\nZweite Zeile\n[/BLOCK 1]") != std::string::npos);
  assert(batch.find("[BLOCK 2]\nLeben: 42\n[/BLOCK 2]") != std::string::npos);
}
