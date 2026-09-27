#include "d4r0/PanelLayout.h"
#include <array>
#include <cassert>

int main() {
  const d4r0::Rect source{10,10,100,20};
  const std::array neighbors{d4r0::Rect{10,42,100,20}};
  auto measure=[](float width,float font) { return width>=160 ? font : font*3; };
  const auto expanded=d4r0::fitPanel(source,neighbors,{0,0,400,200},24,measure);
  assert(expanded.bounds.width>=170);
  assert(expanded.bounds.y+expanded.bounds.height<=neighbors[0].y);
  assert(expanded.fontPx==24);
  const auto constrained=d4r0::fitPanel(source,neighbors,{0,0,150,70},24,measure);
  assert(constrained.bounds.width==0); // No readable collision-free placement.
  const d4r0::Rect headline{2352,283,743,106};
  const std::array metadata{d4r0::Rect{2944,391,130,30}};
  const auto compact=d4r0::fitPanel(headline,metadata,{0,0,3840,2160},32,
      [](float,float){ return 76.0F; });
  assert(compact.bounds.width && compact.bounds.y+compact.bounds.height<=metadata[0].y);
  const auto shrunk=d4r0::fitPanel({100,100,200,55},{},{0,0,500,500},24,
      [](float,float font){ return font*2.2F; });
  assert(shrunk.bounds.width==210);
  assert(shrunk.bounds.height==61);
  assert(shrunk.fontPx==24); // 24px fits with padding in the original card.
  const auto smaller=d4r0::fitPanel({100,100,200,55},{},{0,0,500,500},30,
      [](float,float font){ return font*2.2F; });
  assert(smaller.bounds.width==210);
  assert(smaller.fontPx==24); // Shrink before spreading across the page.
  const auto minimum=d4r0::fitPanel({100,100,200,32},{},{0,0,500,500},13,
      [](float,float font){ return font*2.4F; });
  assert(minimum.fontPx==12); // The readable lower bound is checked even from an odd starting size.
}
