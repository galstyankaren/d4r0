# Local runtime verification

Downloaded assets live under ignored `local-assets/`; do not commit them.

## Pinned assets

| Asset | Revision | Verification |
| --- | --- | --- |
| ggml-org/llama.cpp Windows Vulkan | b11068 | Archive SHA-256 `26527dd7ee8c66de1eff5b169338fec1b8d0fe48c7bec0edb98dbac9aecc844a` |
| mradermacher/translategemma-4b-it-GGUF, Q4_K_M | 35a7486e128b19642cdc72d7b91b21ba388aaf42 | File SHA-256 `81200d03e843d2ec1ece6eeafe7d13cb6e5211e1fcd336ade55790b683a08330` |
| PaddlePaddle/latin_PP-OCRv5_mobile_rec_onnx | 89d3a50e2c27e2e7cceeab0e944c25c807d5db4f | Official inference.onnx and inference.yml downloaded |
| PaddlePaddle/PP-OCRv5_mobile_det_onnx | e6f4fa85f00e168c862bc462aebca69eef9b3d3d | Official inference.onnx and inference.yml downloaded |
| Microsoft.ML.OnnxRuntime.DirectML | NuGet 1.24.4 | Native x64 library used by the OCR smoke test |
| Microsoft.AI.DirectML | NuGet 1.15.4 | Runtime dependency copied beside the test executable |

The b11068 ROCm archive also downloaded and matched its published SHA-256,
but `ggml-hip.dll` cannot load because `hipblas.dll` is absent. Vulkan detects
the RX 9070 XT as Vulkan0. This backend choice does not change the selected model.

## Reproduce the synthetic translation check

From the repository root, run in a terminal:

```powershell
.\local-assets\llama-b11068-vulkan\llama-server.exe -m local-assets/translategemma-4b-it.Q4_K_M.gguf --device Vulkan0 -ngl 99 -c 4096 -np 1 --host 127.0.0.1 --port 18765 --no-webui --no-jinja --chat-template gemma --log-disable
```

In another terminal:

```powershell
.\scripts\Test-LocalTranslation.ps1
```

Stop the test server with Ctrl+C afterward. The application itself now owns its
server process, uses a random in-memory bearer key on loopback, suppresses server
logs, and terminates the process tree at application shutdown.

The test uses `/completion` with TranslateGemma's embedded German-to-English
prompt format. The generic chat-template setting only lets the server initialize;
it is not used to construct completion prompts. The model's strict template fails
llama.cpp b11068's automatic chat parser, including with `--skip-chat-parsing`.

Observed result: `Die Welt ist voller Wunder.` -> `The world is full of wonders.`
First request reported 540.625 ms prompt time and 41.856 ms generation time.
These are synthetic inference timings, not game benchmarks or v1 acceptance evidence.
This standalone script remains useful for isolating model/runtime failures from
the connected live screen pipeline.

## Native crop OCR and GPU checks

The Debug CTest suite now exercises the actual PP-OCR detector and recognizer
on CPU using two synthetic German lines, plus an empty crop and invalid inputs.
The equivalent DirectML run also passed on this machine:

```powershell
.\out\d4r0_ocr_smoke.exe local-assets/ocr-rec/inference.onnx local-assets/ocr-rec/inference.yml local-assets/ocr-det/inference.onnx 0
```

Detector preprocessing follows the pinned model's `inference.yml` (BGR, channel
normalization, dimensions rounded to multiples of 32). Postprocessing currently
uses connected components and padded axis-aligned boxes, not rotated polygon
rectification. It keeps low-confidence boxes rather than silently hiding them.
Rotated text, outlined game fonts, and moving backgrounds still need validation.

`d4r0_gpu_tests` uses synthetic GPU textures to check 64-pixel tile differences,
noise tolerance, cumulative slow fades, edge tiles, resize, and selected-crop BGRA readback. Only change
counts are read back by the differ; the crop API rejects full-frame readback.
`d4r0_core_tests` checks stability delay, bounded batches, retries, no duplicate
in-flight jobs, stale retry attempts, unchanged-screen suppression, and revision invalidation.

The opt-in capture test briefly displays synthetic colored squares. It checks a
held GPU snapshot through subsequent captured color changes, an excluded overlay,
concurrent snapshot reads during stop, and three additional start/stop cycles.
Only bounded test-square crops are read back; no screenshots are saved:

```powershell
.\out\d4r0_capture_smoke.exe
```

It passed in the interactive Windows session. The capture callback now owns a
GPU copy instead of holding a reusable frame-pool surface. Monitor resize/device
loss and real game performance still need broader validation.
The component checks below are complemented by the connected live test.

## Connected live pipeline and replay

The native pipeline now connects owned WGC snapshots to GPU tile differences,
stability scheduling, bounded crop readback, PP-OCR detection/recognition, an
authenticated loopback-only owned llama-server, revision-safe cache replacement,
and the excluded click-through overlay. `d4r0_live_smoke.exe local-assets` passed
with a synthetic on-screen German sentence and exercised original/translation
mode restoration. The actual Debug app also stayed active with capture, model,
overlay, and replay together and exited cleanly through `Ctrl+Alt+Q`.

`d4r0_replay_smoke.exe` passed against the installed AMD Media Foundation H.264
encoder. It tests GPU BGRA-to-1080p-NV12 conversion, hardware-only encoding,
twenty-segment ring eviction, MP4 finalization, dead-process ring cleanup, and
directory deletion at object exit. No real screen pixels are used by this test.

The diagnostics overlay reports live frame interval, GPU tile-difference time,
OCR time, model time, detector-crop count, model-request count, recognized-line
count, visible region count, stale jobs, and replay state. Stable work is grouped
into overlapping 960x540 detector cores, so a 3840x2160 viewport is covered by
bounded crops rather than one OCR pass per 64x64 change tile. Translation runs
asynchronously while capture changes continue to invalidate stale regions.
Use `Ctrl+Alt+D` to toggle it. Runtime logs contain lifecycle/errors only and do
not include OCR text, translations, screenshots, model secrets, or timestamps.

The verified Release multi-line WGC smoke test translated ten synthetic German
lines in 1,187 ms from the ready signal on the target interactive Windows
session. This is a browser-style baseline; game contention and text quality on
small or animated fonts still require representative game measurement.
