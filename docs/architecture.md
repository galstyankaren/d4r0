# Architecture and POC validation

The work queue is deliberately change-driven:

```
Windows Graphics Capture -> GPU tile differ -> stability/motion scheduler -> PP-OCR crops
    -> region merger -> llama.cpp TranslateGemma batch -> region cache -> D3D/DirectComposition overlay
```

`TextRegion` is revisioned so an older OCR/translation job cannot overwrite a newer screen region. Normal processing keeps full captured frames GPU-resident. A ready tile triggers OCR of its full-width half-screen strip, with 64 px of vertical overlap; the GPU adapter reads back only that bounded strip. Two strips cover a 4K screen without cutting long article lines at a vertical tile boundary. Explicit diagnostic capture can read back full frames for local PNGs.

Tiles that keep changing receive one bounded OCR attempt about every 900 ms. OCR candidates need two spatially consistent observations before translation; a long, narrow scrolling line can also confirm through a shifted overlap of its visible text. Evidence lasts long enough for a dense page's translation pass, while an unrelated text change at the same position resets it. Small, weak recognition results receive one contrast retry. Detector fragments below the geometry and confidence floor are rejected before translation. Accepted text is translated regardless of language. Moving-tile jobs retain their revision while in flight, then advance it for the next sample; pixel motion alone does not erase the last verified translation. Confirmed lines from both half-screen strips are grouped together, so a paragraph can cross their boundary. Grouping keeps aligned paragraph lines within a plausible column and separates headings from descriptions using line size and spacing. The overlay measures translated text with DirectWrite and first tries a readable font of at least 12 px within the source area. If it cannot fit, the renderer places a measured panel in free screen space. Each panel uses a blurred source snapshot with a translucent contrast tint; an opaque panel remains the fallback if capture is unavailable.

## Translation profiles

Capture remains monitor-wide. The foreground window on the captured monitor
provides a local executable identity for profile selection. A saved executable
profile wins over a category template; recognized browsers, game installation
paths, and professional tools use their editable category defaults, and all
other apps use General. The controls window retains the last captured app while
it has focus. Browser tabs share the browser's executable profile.

Each profile supplies a source and target language and optional style or
terminology instructions. The source-language choices are restricted to the
installed Latin OCR recognizer's supported languages; output choices come
from the configured local TranslateGemma model. Prompt construction always
keeps numbered block framing and protected-token requirements outside the
editable instructions. When the app or effective profile changes, the worker
discards old in-flight results, clears translation and region caches, resets
stability/scheduling, and re-observes the current frame so static text is
translated with the new context. Model selection remains global.

The optional instruction draft uses the already-running loopback model only
after the user requests it. It receives app name, category, language pair,
and optional user description, not captured pixels or OCR text. Drafts are
reviewed in the controls window and are never saved automatically.

## Required local assets

* PP-OCR Latin detector and recognizer exported to ONNX.
* Windows ML/DirectML-compatible OCR provider (CPU fallback is acceptable).
* A GPU-accelerated Windows `llama.cpp` build and a local TranslateGemma 4B or 12B GGUF. Vulkan is the verified backend on the target RX 9070 XT.

The live app must test these on the target Windows machine. Benchmark each selected model independently while a representative 4K game runs at the accepted 30 FPS case; report p1/p99 frame times, GPU engine use, VRAM residency/paging, capture/OCR/translation/render timings, and dropped jobs. Results are diagnostic only: they must never change the model selected by the user.

## Privacy and replay

The replay writer accepts only source-capture D3D11 textures. A D3D11 video processor scales and converts them to 1080p NV12 on the GPU, and Media Foundation writes a twenty-segment (30 seconds each), 30 FPS H.264 ring. It rejects software encoders. Overlay pixels, OCR text, translations, screenshots outside the ring, and telemetry are never accepted by its API. Its process-specific directory is deleted at clean exit, and a later launch removes rings whose owner PID is confirmed dead.

Diagnostic capture is separate from replay and starts only after an explicit toggle during the current launch. For crash diagnosis, pressing the translation toggle currently enables debug automatically; the debug hotkey and tray control can also enable it. It saves local source and rendered-overlay PNG pairs about every two seconds and at OCR batches (at most twice per second), plus JSONL records containing recognized text, translations, geometry, timings, and errors under `%LOCALAPPDATA%\d4r0\diagnostics`. Files persist until cleared by the user or removed by the 2 GB retention limit; completed older sessions are removed first. Disabling debug stops writes immediately. No diagnostic data is uploaded. The debug setting is always off at startup even if an older settings file saved it as on.

## Unsupported v1 modes

HDR, exclusive fullscreen, game injection, anti-cheat circumvention, cloud translation, and macOS are out of scope.
