#include "d4r0/RegionCache.h"
#include "d4r0/TranslationPrompt.h"
#include <cassert>
int main() {
  d4r0::RegionCache cache;
  assert(cache.upsert({.stableId=7, .german="Speichern", .revision=2}));
  assert(!cache.upsert({.stableId=7, .german="old", .revision=1}));
  assert(cache.visible().front().german == "Speichern");
  const auto prompt = d4r0::makeTranslationPrompt({"Druecke {key}", "Leben: 42"});
  assert(prompt.find("{key}") != std::string::npos);
  const auto parsed = d4r0::parseNumberedTranslations("1. Press {key}\n2. Health: 42", 2);
  assert(parsed[0] == " Press {key}" && parsed[1] == " Health: 42");
}
