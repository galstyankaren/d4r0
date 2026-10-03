#pragma once
#include "d4r0/RegionCache.h"
#include "d4r0/Settings.h"
#include "d4r0/WindowsGraphicsCapture.h"
#include "d4r0/DiagnosticSession.h"
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>
#include <optional>

namespace d4r0 {
class LivePipeline {
 public:
  LivePipeline(PipelineSettings settings, WindowsGraphicsCapture& capture, RegionCache& cache,
               DiagnosticSession& diagnostics,
               std::function<void(std::wstring)> status,
               std::function<void(ActiveProfile)> activeProfile = {},
               std::function<void(ProfileDraftResult)> draftResult = {});
  ~LivePipeline();
  LivePipeline(const LivePipeline&) = delete;
  void stop();
  void updateProfiles(std::vector<TranslationProfile> profiles);
  void requestDraft(ProfileDraftRequest request);
 private:
  void run(std::stop_token stop);
  PipelineSettings settings_;
  WindowsGraphicsCapture& capture_;
  RegionCache& cache_;
  DiagnosticSession& diagnostics_;
  std::function<void(std::wstring)> status_;
  std::function<void(ActiveProfile)> activeProfile_;
  std::function<void(ProfileDraftResult)> draftResult_;
  std::mutex profileMutex_;
  std::vector<TranslationProfile> profiles_;
  std::optional<ProfileDraftRequest> pendingDraft_;
  std::atomic<std::uint64_t> profileVersion_{1};
  std::jthread worker_;
};
}
