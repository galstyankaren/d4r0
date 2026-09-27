#pragma once
#include "d4r0/Types.h"
#include <functional>
#include <span>

namespace d4r0 {
struct PanelPlacement { Rect bounds; float fontPx{}; };
// measure returns wrapped text height for the proposed inner width and font.
[[nodiscard]] PanelPlacement fitPanel(const Rect& source, std::span<const Rect> otherText,
                                      const Rect& viewport, float preferredFontPx,
                                      const std::function<float(float,float)>& measure);
}
