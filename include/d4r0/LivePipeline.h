#pragma once
#include "d4r0/RegionCache.h"
#include "d4r0/Settings.h"
#include "d4r0/WindowsGraphicsCapture.h"
#include <thread>
#include <functional>

namespace d4r0 {
class LivePipeline {
 public:
  LivePipeline(PipelineSettings settings, WindowsGraphicsCapture& capture, RegionCache& cache,
               std::function<void(std::wstring)> status);
  ~LivePipeline();
  LivePipeline(const LivePipeline&) = delete;
  void stop();
 private:
  void run(std::stop_token stop);
  PipelineSettings settings_;
  WindowsGraphicsCapture& capture_;
  RegionCache& cache_;
  std::function<void(std::wstring)> status_;
  std::jthread worker_;
};
}
