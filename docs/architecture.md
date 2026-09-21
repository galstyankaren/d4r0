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
* A GPU-accelerated Windows `llama.cpp` build and a local TranslateGemma 4B or 12B GGUF. Vulkan is the verified backend on the target RX 9070 XT.

The live app must test these on the target Windows machine. Benchmark each selected model independently while a representative 4K game runs at the accepted 30 FPS case; report p1/p99 frame times, GPU engine use, VRAM residency/paging, capture/OCR/translation/render timings, and dropped jobs. Results are diagnostic only: they must never change the model selected by the user.

## Privacy and replay

The replay writer accepts only source-capture D3D11 textures. A D3D11 video processor scales and converts them to 1080p NV12 on the GPU, and Media Foundation writes a twenty-segment (30 seconds each), 30 FPS H.264 ring. It rejects software encoders. Overlay pixels, OCR text, translations, screenshots outside the ring, and telemetry are never accepted by its API. Its process-specific directory is deleted at clean exit, and a later launch removes rings whose owner PID is confirmed dead.

## Unsupported v1 modes

HDR, exclusive fullscreen, game injection, anti-cheat circumvention, cloud translation, and macOS are out of scope.
