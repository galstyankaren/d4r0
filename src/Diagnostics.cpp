#include "d4r0/Diagnostics.h"

#include <algorithm>
#include <utility>

namespace d4r0 {
void DiagnosticsStore::setCapture(CaptureSnapshot capture) {
  std::scoped_lock lock(mutex_);
  data_.capture = capture;
}

void DiagnosticsStore::setTiming(TimingSnapshot timing) {
  std::scoped_lock lock(mutex_);
  data_.timing = timing;
}

void DiagnosticsStore::updateResource(ResourceRow resource) {
  std::scoped_lock lock(mutex_);
  const auto existing = std::find_if(data_.resources.begin(), data_.resources.end(), [&](const auto& row) {
    return row.name == resource.name;
  });
  if (existing == data_.resources.end()) {
    data_.resources.push_back(std::move(resource));
  } else {
    *existing = std::move(resource);
  }
}

void DiagnosticsStore::addCrop(CropDiagnostic crop) {
  std::scoped_lock lock(mutex_);
  constexpr std::size_t maxCrops = 32;
  constexpr std::size_t maxPreviewBytes = 8 * 1024 * 1024;
  if (crop.previewBgra.size() > maxPreviewBytes) crop.previewBgra.clear();
  data_.recentCrops.insert(data_.recentCrops.begin(), std::move(crop));
  if (data_.recentCrops.size() > maxCrops) data_.recentCrops.resize(maxCrops);
  std::size_t bytes{};
  for (const auto& item : data_.recentCrops) bytes += item.previewBgra.size();
  while (bytes > maxPreviewBytes && !data_.recentCrops.empty()) {
    auto& oldest = data_.recentCrops.back();
    bytes -= oldest.previewBgra.size();
    oldest.previewBgra.clear();
    if (bytes > maxPreviewBytes && data_.recentCrops.size() > 1) data_.recentCrops.pop_back();
  }
}

DiagnosticsSnapshot DiagnosticsStore::snapshot() const {
  std::scoped_lock lock(mutex_);
  return data_;
}

void DiagnosticsStore::clear() {
  std::scoped_lock lock(mutex_);
  data_ = {};
}
} // namespace d4r0
