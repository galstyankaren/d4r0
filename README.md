# d4r0

`d4r0` is a local-only German → English game-translation overlay for Windows 11 SDR, borderless, and windowed games. It does not inject into games, read game memory, hook graphics APIs, or bypass anti-cheat.

## Current POC scope

The project contains a buildable native Win32 overlay shell and the pipeline contracts that keep capture, OCR, translation, rendering, diagnostics, and replay independent. The shell creates a non-activating, click-through, topmost overlay and asks Windows to exclude it from capture. `Ctrl+Alt+T` toggles translation rendering; `Ctrl+Alt+O` immediately shows the original game while preserving cached results.

The ML adapters deliberately fail closed until local assets are configured: PP-OCR ONNX detector/recognizer files and a user-selected `llama.cpp` executable plus TranslateGemma GGUF. No network client or telemetry code exists in this repository.

## Build on Windows 11

Install Visual Studio 2022 with the Desktop C++ and Windows SDK workloads, then:

```powershell
cmake -S . -B out -G Ninja
cmake --build out
ctest --test-dir out --output-on-failure
```

The overlay executable is `out/d4r0.exe`. Run it normally; elevated games can prevent normal desktop overlays from appearing above them. HDR and exclusive fullscreen are intentionally unsupported in v1.

## Local configuration

At first launch the app stores settings under `%LOCALAPPDATA%\\d4r0\\settings.ini`. The default selected model slot is `TranslateGemma 4B`; changing it is always explicit. Diagnostics are advisory only and never alter the chosen model.

See [architecture](docs/architecture.md) for extension points and the hardware-validation checklist.
