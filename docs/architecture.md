# Architecture and POC validation

The work queue is deliberately change-driven:

```
Windows Graphics Capture -> GPU tile differ -> stability tracker -> PP-OCR crops
    -> region merger -> llama.cpp TranslateGemma batch -> region cache -> D3D/DirectComposition overlay
```

`TextRegion` is revisioned so an older OCR/translation job cannot overwrite a newer screen region. Full captured frames remain GPU-resident; adapters may read back only selected OCR crops when their selected execution provider needs it.

## Required local assets

* PP-OCR Latin detector and recognizer exported to ONNX.
* Windows ML/DirectML-compatible OCR provider (CPU fallback is acceptable).
* A Windows ROCm/HIP-capable `llama.cpp` build and a local TranslateGemma 4B or 12B GGUF.

The live app must test these on the target Windows machine. Benchmark each selected model independently while a representative 4K game runs at the accepted 30 FPS case; report p1/p99 frame times, GPU engine use, VRAM residency/paging, capture/OCR/translation/render timings, and dropped jobs. Results are diagnostic only: they must never change the model selected by the user.

## Privacy and replay

The replay writer accepts only source-game frames. It is a 10-minute, 1080p/30 FPS circular hardware-encoder integration point; overlay pixels, OCR text, translations, screenshots outside the ring, and telemetry are not persisted. Its directory is deleted at process exit.

## Unsupported v1 modes

HDR, exclusive fullscreen, game injection, anti-cheat circumvention, cloud translation, and macOS are out of scope.
