#include "d4r0/TextGrouping.h"
#include <array>
#include <cassert>

namespace {
void groupsSoftWrappedLines() {
  const std::array lines{
      d4r0::OcrLine{1, {10, 10, 120, 20}, 0.95F, "  Eine   lange  "},
      d4r0::OcrLine{2, {10, 34, 90, 20}, 0.94F, "Zeile\n"},
  };

  const auto groups = d4r0::groupTextLines(lines);

  assert(groups.size() == 1);
  assert(groups[0].members.size() == 2);
  assert(groups[0].members[0].stableId == 1 && groups[0].members[1].stableId == 2);
  assert(groups[0].source == "Eine lange Zeile");
  assert(groups[0].bounds.x == 10 && groups[0].bounds.y == 10);
  assert(groups[0].bounds.width == 120 && groups[0].bounds.height == 44);
}

void separatesParagraphGaps() {
  const std::array lines{
      d4r0::OcrLine{1, {10, 10, 120, 20}, 0.95F, "Erster Absatz"},
      d4r0::OcrLine{2, {10, 47, 120, 20}, 0.95F, "Zweiter Absatz"},
  };

  const auto groups = d4r0::groupTextLines(lines);

  assert(groups.size() == 2);
  assert(groups[0].source == "Erster Absatz");
  assert(groups[1].source == "Zweiter Absatz");
}

void separatesColumns() {
  const std::array lines{
      d4r0::OcrLine{1, {10, 10, 100, 20}, 0.95F, "Links eins"},
      d4r0::OcrLine{2, {210, 10, 100, 20}, 0.95F, "Rechts eins"},
      d4r0::OcrLine{3, {10, 34, 100, 20}, 0.95F, "Links zwei"},
      d4r0::OcrLine{4, {210, 34, 100, 20}, 0.95F, "Rechts zwei"},
  };

  const auto groups = d4r0::groupTextLines(lines);

  assert(groups.size() == 2);
  assert(groups[0].source == "Links eins Links zwei");
  assert(groups[1].source == "Rechts eins Rechts zwei");
}

void separatesSameRowNavigation() {
  const std::array lines{
      d4r0::OcrLine{1, {10, 10, 70, 20}, 0.95F, "Spielen"},
      d4r0::OcrLine{2, {100, 10, 90, 20}, 0.95F, "Optionen"},
      d4r0::OcrLine{3, {210, 10, 80, 20}, 0.95F, "Beenden"},
  };

  const auto groups = d4r0::groupTextLines(lines);

  assert(groups.size() == 3);
  assert(groups[0].source == "Spielen");
  assert(groups[1].source == "Optionen");
  assert(groups[2].source == "Beenden");
}

void isDeterministicUnderShuffledInput() {
  const std::array ordered{
      d4r0::OcrLine{1, {10, 10, 100, 20}, 0.95F, "Links eins"},
      d4r0::OcrLine{2, {210, 10, 100, 20}, 0.95F, "Rechts eins"},
      d4r0::OcrLine{3, {10, 34, 100, 20}, 0.95F, "Links zwei"},
      d4r0::OcrLine{4, {210, 34, 100, 20}, 0.95F, "Rechts zwei"},
  };
  const std::array shuffled{ordered[3], ordered[0], ordered[2], ordered[1]};

  const auto first = d4r0::groupTextLines(ordered);
  const auto second = d4r0::groupTextLines(shuffled);

  assert(first.size() == second.size());
  for (std::size_t group = 0; group < first.size(); ++group) {
    assert(first[group].source == second[group].source);
    assert(first[group].members.size() == second[group].members.size());
    for (std::size_t member = 0; member < first[group].members.size(); ++member)
      assert(first[group].members[member].stableId == second[group].members[member].stableId);
  }
}

void deduplicatesHeavyOverlaps() {
  const std::array lines{
      d4r0::OcrLine{8, {10, 10, 120, 20}, 0.70F, "Dialag"},
      d4r0::OcrLine{7, {11, 10, 120, 20}, 0.96F, "Dialog"},
  };

  const auto groups = d4r0::groupTextLines(lines);

  assert(groups.size() == 1);
  assert(groups[0].members.size() == 1);
  assert(groups[0].members[0].stableId == 7);
  assert(groups[0].source == "Dialog");
  assert(groups[0].bounds.x == 11 && groups[0].bounds.width == 120);
}

void capsGroupsAtEightLines() {
  std::vector<d4r0::OcrLine> lines;
  for (std::uint64_t id = 1; id <= 10; ++id)
    lines.push_back({id, {10, 10 + static_cast<float>((id - 1) * 24), 100, 20},
                     0.95F, "Zeile"});

  const auto groups = d4r0::groupTextLines(lines);

  assert(groups.size() == 2);
  assert(groups[0].members.size() == 8 && groups[1].members.size() == 2);
  assert(groups[0].members.front().stableId == 1 && groups[0].members.back().stableId == 8);
  assert(groups[1].members.front().stableId == 9 && groups[1].members.back().stableId == 10);
}
}

int main() {
  groupsSoftWrappedLines();
  separatesParagraphGaps();
  separatesColumns();
  separatesSameRowNavigation();
  isDeterministicUnderShuffledInput();
  deduplicatesHeavyOverlaps();
  capsGroupsAtEightLines();
}
