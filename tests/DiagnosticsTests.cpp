#include "d4r0/Diagnostics.h"

#include <cassert>
#include <string>

int main() {
  using d4r0::CropOutcome;
  assert(static_cast<int>(CropOutcome::NoFrame) == 0);
  assert(static_cast<int>(CropOutcome::Unstable) == 1);
  assert(static_cast<int>(CropOutcome::NoCrop) == 2);
  assert(static_cast<int>(CropOutcome::NoBoxes) == 3);
  assert(static_cast<int>(CropOutcome::OutsideCore) == 4);
  assert(static_cast<int>(CropOutcome::EmptyRecognition) == 5);
  assert(static_cast<int>(CropOutcome::LowConfidence) == 6);
  assert(static_cast<int>(CropOutcome::Accepted) == 7);
  assert(static_cast<int>(CropOutcome::Stale) == 8);
  assert(static_cast<int>(CropOutcome::TranslationFailure) == 9);
  assert(static_cast<int>(CropOutcome::ReadbackFailure) == 10);

  d4r0::DiagnosticsStore store;
  store.setCapture({
    .state = d4r0::CaptureState::Capturing,
    .width = 3840,
    .height = 2160,
    .frameRevision = 42,
  });
  store.setTiming({
    .captureMs = 1.25,
    .changeDetectMs = 0.5,
    .ocrMs = 7.0,
    .translationMs = 12.0,
    .renderMs = 0.75,
    .droppedCapture = 1,
    .droppedOcr = 2,
    .droppedTranslation = 3,
    .staleResults = 4,
  });
  store.updateResource({.name = "GPU", .bytes = 1024, .detail = "D3D11"});
  store.updateResource({.name = "model", .bytes = 2048, .detail = "loaded"});
  store.updateResource({.name = "GPU", .bytes = 4096, .detail = "resident"});
  store.addCrop({
    .sequence = 1,
    .outcome = CropOutcome::NoBoxes,
    .width = 2,
    .height = 1,
    .previewBgra = {1, 2, 3, 4, 5, 6, 7, 8},
    .detectorBoxes = 0,
    .detectorMs = 0.4,
  });
  store.addCrop({
    .sequence = 2,
    .outcome = CropOutcome::Accepted,
    .detectorBoxes = 2,
    .detectorMs = 0.7,
    .recognizedLines = 2,
    .recognizerMs = 1.2,
    .confidence = 0.93F,
  });

  auto snapshot = store.snapshot();
  assert(snapshot.capture.state == d4r0::CaptureState::Capturing);
  assert(snapshot.capture.width == 3840 && snapshot.capture.height == 2160);
  assert(snapshot.capture.frameRevision == 42);
  assert(snapshot.timing.captureMs == 1.25 && snapshot.timing.staleResults == 4);
  assert(snapshot.resources.size() == 2);
  assert(snapshot.resources[0].name == "GPU" && snapshot.resources[0].bytes == 4096);
  assert(snapshot.resources[0].detail == "resident");
  assert(snapshot.resources[1].name == "model");
  assert(snapshot.recentCrops.size() == 2);
  assert(snapshot.recentCrops[0].sequence == 2);
  assert(snapshot.recentCrops[0].recognizedLines == 2);
  assert(snapshot.recentCrops[1].sequence == 1);
  assert(snapshot.recentCrops[1].previewBgra.size() == 8);
}
