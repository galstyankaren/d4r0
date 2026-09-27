#include "d4r0/PanelLayout.h"
#include <algorithm>

namespace d4r0 {
namespace {
bool intersects(const Rect& a, const Rect& b) {
  return a.x < b.x+b.width && b.x < a.x+a.width &&
         a.y < b.y+b.height && b.y < a.y+a.height;
}
}
PanelPlacement fitPanel(const Rect& source, std::span<const Rect> otherText,
                        const Rect& viewport, float preferredFontPx,
                        const std::function<float(float,float)>& measure) {
  const float minWidth=std::max(30.0F,source.width+10);
  const float maxWidth=std::min(minWidth+320,viewport.width-8);
  if (maxWidth<minWidth || viewport.height<20) return {};
  const Rect inPlace{source.x-5,source.y-3,minWidth,source.height+6};
  const auto free = [&](const Rect& candidate) {
    return candidate.x>=viewport.x && candidate.y>=viewport.y &&
        candidate.x+candidate.width<=viewport.x+viewport.width &&
        candidate.y+candidate.height<=viewport.y+viewport.height &&
        std::none_of(otherText.begin(),otherText.end(),[&](const Rect& other) {
          return intersects(candidate,other);
        });
  };
  // Keep the translation on its source card whenever a readable font fits.
  for(float font=std::clamp(preferredFontPx,12.0F,48.0F);font>=12.0F;
      font=font==12.0F ? 10.0F : std::max(12.0F,font-2.0F)) {
    if (measure(std::max(20.0F,minWidth-10),font)+8<=inPlace.height && free(inPlace))
      return {inPlace,font};
  }
  for(float font=std::clamp(preferredFontPx,12.0F,48.0F);font>=12.0F;
      font=font==12.0F ? 10.0F : std::max(12.0F,font-2.0F)) {
    for(int position=0;position<5;++position) {
      for(float width=minWidth;width<=maxWidth+31.5F;width+=32.0F) {
        width=std::min(width,maxWidth);
        const float height=std::max(font+8,measure(std::max(20.0F,width-10),font)+8);
        const Rect choices[]{
            {source.x-5,source.y-3,width,height},
            {source.x-5,source.y-height-4,width,height},
            {source.x-5,source.y+source.height+4,width,height},
            {source.x+source.width+4,source.y-3,width,height},
            {source.x-width-4,source.y-3,width,height},
        };
        const auto& candidate=choices[position];
        if(free(candidate)) return {candidate,font};
        if(width>=maxWidth) break;
      }
    }
  }
  return {};
}
}
