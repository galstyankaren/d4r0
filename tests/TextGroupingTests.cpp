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
void doesNotJoinAdjacentArticleCards() {
  const std::array lines{
      d4r0::OcrLine{1,{10,10,240,20},0.95F,"A wide headline"},
      d4r0::OcrLine{2,{180,34,180,20},0.95F,"Next card"},
      d4r0::OcrLine{3,{10,34,180,20},0.95F,"Same article"},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==2);
  assert(groups[0].source=="A wide headline Same article");
  assert(groups[1].source=="Next card");
}
void separatesHeadlineFromArticleParagraph() {
  const std::array lines{
      d4r0::OcrLine{1,{708,1288,643,66},0.98F,"Headline one"},
      d4r0::OcrLine{2,{708,1355,666,62},0.99F,"Headline two"},
      d4r0::OcrLine{3,{708,1421,620,70},0.99F,"Headline three"},
      d4r0::OcrLine{4,{708,1493,637,57},0.98F,"Headline four"},
      d4r0::OcrLine{5,{708,1586,661,52},0.99F,"Paragraph one"},
      d4r0::OcrLine{6,{708,1644,462,39},0.99F,"Paragraph two"},
      d4r0::OcrLine{7,{708,1695,644,52},0.95F,"Paragraph three"},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==2);
  assert(groups[0].members.size()==4);
  assert(groups[1].members.size()==3);
}
void separatesSingleLineHeadingFromDescription() {
  const std::array lines{
      d4r0::OcrLine{1,{1355,823,433,60},0.99F,"Puzzle title"},
      d4r0::OcrLine{2,{1353,897,464,52},0.99F,"Testen Sie Ihr Wissen mit"},
      d4r0::OcrLine{3,{1354,952,399,51},0.99F,"the daily puzzle."},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==2);
  assert(groups[0].members.size()==1);
  assert(groups[1].members.size()==2);
}
void keepsParagraphWithVariableLineHeights() {
  const std::array lines{
      d4r0::OcrLine{1,{710,1792,465,47},0.99F,"Employers and"},
      d4r0::OcrLine{2,{711,1850,682,43},0.99F,"unions are alarmed"},
      d4r0::OcrLine{3,{708,1902,703,52},0.99F,"by the education crisis."},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==1 && groups[0].members.size()==3);
}
void separatesLargeUiTitleFromDescription() {
  const std::array lines{
      d4r0::OcrLine{1,{1953,818,367,72},0.99F,"Puzzle"},
      d4r0::OcrLine{2,{1953,893,504,60},0.99F,"Draw rectangles to fit"},
      d4r0::OcrLine{3,{1954,952,377,51},0.99F,"the numbers"},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==2 && groups[0].members.size()==1 && groups[1].members.size()==2);
}
void separatesMultilineHeadlineFromParagraph() {
  const std::array lines{
      d4r0::OcrLine{1,{2354,1122,595,61},0.99F,"Headline one"},
      d4r0::OcrLine{2,{2352,1186,681,61},0.99F,"Headline two"},
      d4r0::OcrLine{3,{2352,1250,677,61},0.99F,"Headline three"},
      d4r0::OcrLine{4,{2350,1310,368,68},0.99F,"Headline four"},
      d4r0::OcrLine{5,{2353,1401,706,52},0.99F,"Paragraph one"},
      d4r0::OcrLine{6,{2353,1458,696,51},0.99F,"Paragraph two"},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==2 && groups[0].members.size()==4 && groups[1].members.size()==2);
}
void separatesCategoryFromHeadline() {
  const std::array lines{
      d4r0::OcrLine{1,{709,1784,487,37},0.99F,"Category"},
      d4r0::OcrLine{2,{708,1835,699,48},0.99F,"Headline one"},
      d4r0::OcrLine{3,{708,1882,619,48},0.99F,"Headline two"},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==2 && groups[0].members.size()==1 && groups[1].members.size()==2);
}
void joinsWrappedWordAfterInlineBadge() {
  const std::array lines{
      d4r0::OcrLine{1,{2461,413,626,52},0.99F,"JU-Chef Winkel will Migranten we-"},
      d4r0::OcrLine{2,{2351,468,522,52},0.99F,"niger Sozialleistungen zahlen"},
  };
  const auto groups=d4r0::groupTextLines(lines);
  assert(groups.size()==1 && groups[0].members.size()==2);
  assert(groups[0].source=="JU-Chef Winkel will Migranten weniger Sozialleistungen zahlen");
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
  doesNotJoinAdjacentArticleCards();
  separatesHeadlineFromArticleParagraph();
  separatesSingleLineHeadingFromDescription();
  keepsParagraphWithVariableLineHeights();
  separatesLargeUiTitleFromDescription();
  separatesMultilineHeadlineFromParagraph();
  separatesCategoryFromHeadline();
  joinsWrappedWordAfterInlineBadge();
}
