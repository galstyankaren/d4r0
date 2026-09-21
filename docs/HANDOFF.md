# d4r0 takeover prompt

## Current local checkpoint (supersedes historical state below)

- Do not push or change GitHub state. Save tested work in local commits. Only
  on an explicit manual push request, squash unpublished checkpoints into one
  commit; preserve published history. No public activity during 09:00–19:00
  Europe/Berlin. Do not falsify timestamps.
- Translation toggle is **Ctrl+Shift+Tab**, not the original Ctrl+Alt+T below.
- Windows Debug build and tests work. WGC captures owned, immutable GPU
  snapshots, with weak callback ownership and shutdown outside the callback lock.
- `GpuRegions` compares 64x64 GPU tiles and reads only selected OCR crops.
  `RegionScheduler` provides stability gating, bounded batches, retry, and stale
  job rejection. Both are connected to the live application's worker.
- Stable jobs are coalesced into overlapping 960x540 OCR cores and translated
  by screen-level numbered batches. Capture observation continues while model
  requests run, source revisions cover every tile touched by a text region, and
  failed items remain untranslated after one bounded retry.
- Native PP-OCRv5 Latin detection/recognition passes synthetic two-line tests on
  CPU and DirectML. Axis-aligned detection is implemented; rotated text is not.
- TranslateGemma 4B Q4_K_M passes a local Vulkan llama-server synthetic test.
  The downloaded ROCm build cannot load its HIP dependency. See
  `docs/local-runtime.md` for pinned assets, commands, and limitations.
- Assets remain ignored in `local-assets/`. Never commit models, runtime
  binaries, keys, screenshots, screen text, or application logs.
- Next critical integration: selected stable crops -> native OCR -> owned local
  model process -> revision-safe cache -> overlay refresh is now connected and
  passes a synthetic on-screen integration test. Configured shortcuts, local
  diagnostics, per-job failure isolation, and a source-GPU-only hardware H.264
  replay ring are implemented and tested. The Release executable and its live
  translation/replay smokes pass on the target machine. Remaining validation is
  broader real-game/resize/device-loss coverage and representative game
  contention measurement.
  measurement. Region styling remains an adaptive
  panel fallback; rotated-text rectification and background reconstruction are
  known quality limits rather than falsely claimed features.

The historical specification below still defines the full product boundary;
its initial implementation inventory is outdated, not evidence of completion.

Copy the prompt below into the next coding tool after cloning this repository.

---

You are taking over **d4r0**, a Windows 11 native C++20 German→English game-translation overlay. Read `README.md`, `docs/architecture.md`, and the latest Git history before changing code.

## Product boundary

Build for Windows 11 SDR, borderless/windowed games only. The app must stay entirely local. Do not inject into games, hook graphics APIs, read game memory, bypass anti-cheat, add cloud translation, or promise HDR/exclusive-fullscreen support. The overlay should be topmost, non-activating, click-through, and excluded from capture. `Ctrl+Alt+T` toggles translation mode; `Ctrl+Alt+O` immediately shows the original German screen while retaining cached translations.

## Repository state

The current `main` branch contains the initial POC commits:

* `649d43c` — native overlay POC and portable core
* `5c04474` — Windows build instructions
* `4e76e01` — concise README

The GitHub remote is already configured and pushed. Do not commit model files, OCR files, private keys, or telemetry.

## Implemented now

* CMake C++20 project in `CMakeLists.txt`.
* Portable domain types in `include/d4r0/Types.h`: `TextRegion`, `PipelineSettings`, `PerformanceSnapshot`, model and display modes.
* Revision-safe concurrent `RegionCache`.
* Constrained numbered translation prompt/parser.
* Local settings persistence under `%LOCALAPPDATA%\\d4r0\\settings.ini`; new installations default to TranslateGemma 4B and never auto-switch models.
* Win32 overlay shell in `src/OverlayWindow.cpp`: D3D11 device, DirectComposition swap chain, D2D/DirectWrite panels, click-through/topmost/no-activate window, `WDA_EXCLUDEFROMCAPTURE`, hotkeys, original mode, low-confidence dotted treatment.
* `ReplayBuffer` is only a privacy-scoped seam at present; it does not yet encode frames.
* Portable tests in `tests/CoreTests.cpp` cover stale region writes and prompt parsing.

## Known limitations (do not mistake these for completed features)

The executable is a shell, not the finished translator. There is no Windows Graphics Capture frame-pool implementation, GPU tile-change detector, stability tracker, PP-OCR ONNX/Windows ML adapter, llama.cpp process/runtime adapter, region merger, diagnostics panel/charts, Media Foundation circular encoder, or complete replacement-background reconstruction. Add each behind a small concrete seam and test it on the target 9950X/RX 9070 XT/64 GB machine.

The current renderer uses readable adaptive panels as the safe fallback; it does not yet erase and reconstruct original glyph backgrounds. Keep low-confidence output visible with reduced opacity/dotted treatment.

## Decisions already made

* **Native Windows stack:** C++20/Win32/D3D11/DirectComposition was chosen to keep capture, GPU work, and the transparent overlay close to Windows APIs. Do not replace it with a browser or cross-platform UI layer.
* **Capture boundary:** Windows Graphics Capture is the intended source. The overlay uses `WDA_EXCLUDEFROMCAPTURE`; no injection, Present hook, game memory, or anti-cheat workaround is acceptable.
* **Work scheduling:** process changed and stable regions in batches rather than OCR-ing every rendered frame. All timing, confidence, crop, and batch values should remain live-tunable.
* **Model policy:** TranslateGemma 4B is the new-install default; 12B remains a user-selected persistent option. Diagnostics may advise but must never select, unload, throttle, or override the model.
* **Translation contract:** merge related UI/dialogue regions and use a numbered constrained prompt that preserves variables, numbers, shortcuts, markup, names, and line breaks.
* **Rendering policy:** estimate source style and use background reconstruction when safe. Otherwise use a readable style-adaptive panel. Never hide low-confidence text; use subtle reduced opacity and dotted treatment.
* **Privacy policy:** all processing is local. Replay stores source-game pixels only, never the composited overlay, recognized text, translations, screenshots outside the ring, or network telemetry; delete it on exit.
* **Scope exclusions:** HDR, exclusive fullscreen, macOS, cloud services, and anti-cheat-sensitive integration are explicitly deferred.

## Important implementation gaps to preserve in planning

The settings model already has configurable shortcut fields, but the current shell registers `Ctrl+Alt+T` and `Ctrl+Alt+O` directly; make those settings effective before calling shortcut configurability complete. The current `Pipeline.h` contains future-facing interfaces with no concrete consumers; keep or remove them based on the next concrete seam, rather than expanding abstractions. `ReplayBuffer` is a privacy boundary only and must not be described as a working encoder until Media Foundation output exists.

## Immediate next milestones

1. Build on Windows with the commands in `README.md`; fix Windows SDK/compiler errors first.
2. Add Windows Graphics Capture for the selected display and verify the overlay never captures itself.
3. Add GPU-resident tile differencing and changed/stable crop scheduling. Keep cadence, stability delay, thresholds, detector size, and batch sizes live-tunable.
4. Add PP-OCR Latin detector/recognizer ONNX execution with Windows ML/DirectML and CPU fallback. Populate `TextRegion` with polygons, confidence, style estimate, and revision.
5. Add a local llama.cpp TranslateGemma runner for explicitly selected 4B/12B GGUF models. Preserve variables, numbers, shortcuts, markup, names, and line breaks. Never change or throttle the selected model automatically.
6. Add region merging, translation cache, adaptive typesetting, diagnostics, and performance snapshots.
7. Add a real local-only 10-minute 1080p/30 FPS source-frame circular replay using a hardware encoder; delete the replay directory at exit.
8. Benchmark 4B and 12B separately on representative 4K games. Report p1/p99 frame time, GPU/VRAM use, capture/OCR/translation/render timing, and dropped work. Measurements must remain advisory.

## Verification expectations

Run portable tests and, on Windows, `cmake --build out --parallel` plus `ctest --test-dir out --output-on-failure`. Validate multi-monitor/DPI changes, alt-tab, resize, device loss, model-load failure, outlined/animated/low-confidence German text, immediate mode toggling, capture exclusion, no injection, local-only replay, and replay deletion.

## Suggested skills

Use the implementation workflow for feature work, TDD for core scheduling/parsing seams, diagnosing-bugs for Windows runtime failures, code-review before handoff, and research only for primary Windows/AMD/PaddleOCR/TranslateGemma documentation. Keep the implementation minimal and concrete; do not preserve speculative interfaces without a working consumer.

Start by inspecting the current tree and building it on Windows. State which milestone you are implementing before editing.

---

This handoff intentionally contains no credentials or private key material.
