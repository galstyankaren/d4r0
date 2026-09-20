# d4r0

`d4r0` is a local-only German → English game-translation overlay for Windows 11 SDR, borderless, and windowed games. It does not inject into games, read game memory, hook graphics APIs, or bypass anti-cheat.

## Current POC scope

The project contains a buildable native Win32 overlay shell and the pipeline contracts that keep capture, OCR, translation, rendering, diagnostics, and replay independent. The shell creates a non-activating, click-through, topmost overlay and asks Windows to exclude it from capture. `Ctrl+Alt+T` toggles translation rendering; `Ctrl+Alt+O` immediately shows the original game while preserving cached results.

The ML adapters deliberately fail closed until local assets are configured: PP-OCR ONNX detector/recognizer files and a user-selected `llama.cpp` executable plus TranslateGemma GGUF. No network client or telemetry code exists in this repository.

## Move, build, and run on Windows 11

This repository is intended to be built on a Windows 11 machine. Commit and push it from this Mac, then clone it on that machine:

```powershell
git clone https://github.com/YOUR-ACCOUNT/d4r0.git
cd d4r0
```

Install these prerequisites first:

* Visual Studio 2022 (17.8 or newer) with **Desktop development with C++**.
* Windows 11 SDK (10.0.22621 or newer) and the MSVC v143 build tools.
* CMake 3.24+ and Ninja. They can be installed through the Visual Studio Installer's individual components, or with `winget install Kitware.CMake Ninja-build.Ninja`.
* A current AMD Adrenalin driver. For the local model runtime, separately obtain a Windows ROCm/HIP-capable `llama.cpp` build suitable for the RX 9070 XT.

Open **x64 Native Tools Command Prompt for VS 2022**, then configure, build, and test:

```bat
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out --parallel
ctest --test-dir out --output-on-failure
```

The executable is `out\\d4r0.exe`. Run it normally (not as Administrator unless the game is elevated):

```bat
.\\out\\d4r0.exe
```

On its first launch, d4r0 writes `%LOCALAPPDATA%\\d4r0\\settings.ini`. It defaults to the TranslateGemma 4B selection and never switches models automatically. Configure the paths in that file before connecting the translation runtime:

```ini
llamaExecutable=C:\\tools\\llama.cpp\\llama-cli.exe
model4b=D:\\models\\translategemma-4b-q4.gguf
model12b=D:\\models\\translategemma-12b-q4.gguf
```

Keep models outside the Git repository: they are large local assets and may have their own licence terms. The next development milestone also needs local PP-OCR Latin detector and recognizer ONNX files; see [architecture](docs/architecture.md#required-local-assets).

`Ctrl+Alt+T` toggles translated-overlay mode. `Ctrl+Alt+O` immediately shows the original screen while keeping prior translations cached. Elevated games can prevent a normal desktop overlay from appearing above them. HDR and exclusive fullscreen are intentionally unsupported in v1.

## Local configuration

At first launch the app stores settings under `%LOCALAPPDATA%\\d4r0\\settings.ini`. The default selected model slot is `TranslateGemma 4B`; changing it is always explicit. Diagnostics are advisory only and never alter the chosen model.

See [architecture](docs/architecture.md) for extension points and the hardware-validation checklist.
