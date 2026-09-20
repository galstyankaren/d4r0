#pragma once
#include "d4r0/Settings.h"
#include "d4r0/Types.h"
#include <functional>
#include <span>

namespace d4r0 {
struct ChangedCrop { Rect screenBounds; std::uint64_t contentRevision{}; }; // GPU texture ownership stays adapter-private.
class IFrameCapture { public: virtual ~IFrameCapture() = default; virtual bool start() = 0; virtual void stop() = 0; };
class IOcrEngine { public: virtual ~IOcrEngine() = default; virtual std::vector<TextRegion> recognize(std::span<const ChangedCrop>) = 0; };
class ITranslationEngine { public: virtual ~ITranslationEngine() = default; virtual std::vector<std::string> translate(std::span<const std::string>) = 0; };
// Implementations must use settings as given. They must not change selectedModel based on performance.
class IOverlayRenderer { public: virtual ~IOverlayRenderer() = default; virtual void render(std::span<const TextRegion>, DisplayMode) = 0; };
} // namespace d4r0
