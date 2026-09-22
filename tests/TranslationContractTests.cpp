#include "d4r0/TranslationPrompt.h"
#include <cassert>

int main() {
  const auto prompt = d4r0::makeTranslationPrompt({"Ziele:\n1. Finde den Schl\xC3\xBCssel", "Leben: 42"});
  assert(prompt.find("[BLOCK 1]\nZiele:\n1. Finde den Schl\xC3\xBCssel\n[/BLOCK 1]") != std::string::npos);
}
