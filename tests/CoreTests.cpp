#include "d4r0/RegionCache.h"
#include "d4r0/TranslationPrompt.h"
#include "d4r0/RegionScheduler.h"
#include "d4r0/Settings.h"
#include <array>
#include <filesystem>
#include <cassert>
int main() {
  auto shortcut = d4r0::parseShortcut(L"Ctrl+Shift+Tab");
  assert(shortcut && shortcut->modifiers == (d4r0::ShortcutCtrl|d4r0::ShortcutShift) && shortcut->key == 9);
  shortcut = d4r0::parseShortcut(L" alt + F12 ");
  assert(shortcut && shortcut->modifiers == d4r0::ShortcutAlt && shortcut->key == 0x7B);
  assert(!d4r0::parseShortcut(L"Tab"));
  assert(!d4r0::parseShortcut(L"Ctrl+Ctrl+X"));
  assert(!d4r0::parseShortcut(L"Ctrl+Nope"));
  assert(!d4r0::parseShortcut(L"Ctrl+F12junk"));
  const auto settingsPath = std::filesystem::current_path()/"d4r0-settings-test.ini";
  d4r0::PipelineSettings saved;
  saved.toggleShortcut = L"Alt+F12"; saved.originalShortcut = L"Ctrl+O";
  saved.diagnosticsShortcut = L"Ctrl+D"; saved.showDiagnostics = true; saved.replayEnabled = false;
  saved.detectorLongSide = 736; saved.maxOcrBatch = 3; saved.maxTranslationBatch = 4;
  assert(d4r0::SettingsStore(settingsPath).save(saved));
  const auto loaded = d4r0::SettingsStore(settingsPath).load();
  assert(loaded.toggleShortcut == saved.toggleShortcut && loaded.originalShortcut == saved.originalShortcut);
  assert(loaded.diagnosticsShortcut == saved.diagnosticsShortcut && loaded.showDiagnostics && !loaded.replayEnabled);
  assert(loaded.detectorLongSide == 736 && loaded.maxOcrBatch == 3 && loaded.maxTranslationBatch == 4);
  std::filesystem::remove(settingsPath);
  d4r0::RegionCache cache;
  assert(cache.upsert({.stableId=7, .german="Speichern", .revision=2}));
  assert(!cache.upsert({.stableId=7, .german="old", .revision=1}));
  assert(cache.visible().front().german == "Speichern");
  const auto prompt = d4r0::makeTranslationPrompt({"Druecke {key}", "Leben: 42"});
  assert(prompt.find("{key}") != std::string::npos);
  const auto parsed = d4r0::parseNumberedTranslations("1. Press {key}\n2. Health: 42", 2);
  assert(parsed[0] == " Press {key}" && parsed[1] == " Health: 42");
  const auto multiline = d4r0::parseNumberedTranslations("1. First line\nsecond line\n2. Other",2);
  assert(multiline[0] == " First line\nsecond line" && multiline[1] == " Other");
  assert(d4r0::preservesProtectedTokens("Leben: 42 {player} Ctrl+X <b>\nOK",
                                         "Health: 42 {player} Ctrl+X <b>\nOK"));
  assert(!d4r0::preservesProtectedTokens("Leben: 42 {player}","Health: 24 player"));
  d4r0::RegionScheduler scheduler;
  scheduler.reset(2);
  scheduler.observe(std::array<std::uint32_t,2>{10,20},100);
  assert(scheduler.takeReady(279,180,8).empty());
  auto jobs = scheduler.takeReady(280,180,1);
  assert(jobs.size() == 1 && jobs[0].tile == 0);
  const auto stale = jobs[0];
  scheduler.observe(std::array<std::uint32_t,2>{1,0},290);
  assert(!scheduler.current(stale) && !scheduler.complete(stale));
  jobs = scheduler.takeReady(300,180,8);
  assert(jobs.size() == 1 && jobs[0].tile == 1);
  assert(scheduler.takeReady(300,180,8).empty()); // No duplicate in-flight work.
  scheduler.retry(jobs[0]);
  const auto timedOut = jobs[0];
  jobs = scheduler.takeReady(300,180,8);
  assert(!scheduler.complete(timedOut));
  scheduler.retry(timedOut);
  assert(scheduler.takeReady(300,180,8).empty());
  assert(jobs.size() == 1 && scheduler.complete(jobs[0]));
  assert(!scheduler.complete(jobs[0]));
  jobs = scheduler.takeReady(470,180,8);
  assert(jobs.size() == 1 && jobs[0].tile == 0 && scheduler.complete(jobs[0]));
  assert(scheduler.takeReady(999,180,8).empty()); // Unchanged screen is not re-OCRed.
  scheduler.reset(2);
  assert(!scheduler.current(jobs[0])); // A resize also invalidates old results.
  assert(scheduler.takeReady(1000,180,0).empty());
  scheduler.reset(1);
  jobs = scheduler.takeReady(1000,180,1);
  assert(jobs.size() == 1);
  scheduler.retry(jobs[0],2000);
  assert(scheduler.takeReady(2179,180,1).empty());
  jobs = scheduler.takeReady(2180,180,1);
  assert(jobs.size() == 1 && jobs[0].attempt == 2 && scheduler.complete(jobs[0]));
  cache.clear();
  assert(cache.replaceSource(1,10,{{.stableId=1,.german="Test"}}));
  cache.invalidateSource(1,11);
  assert(cache.visible().empty());
  assert(!cache.replaceSource(1,10,{{.stableId=1,.german="stale"}}));
  assert(!cache.upsert({.stableId=1,.sourceId=1,.german="stale",.revision=10}));
  assert(cache.replaceSource(1,11,{{.stableId=1,.german="new"}}));
  assert(cache.visible().front().german == "new");
  cache.invalidateSource(2,20);
  assert(!cache.replaceSources({{1,12,{{.stableId=1,.german="partial"}}},
                                {2,19,{{.stableId=2,.german="stale"}}}}));
  assert(cache.visible().size() == 1 && cache.visible().front().german == "new");
  assert(cache.replaceSources({{1,12,{{.stableId=1,.german="first"}}},
                               {2,20,{{.stableId=2,.german="second"}}}}));
  assert(cache.visible().size() == 2);
}
