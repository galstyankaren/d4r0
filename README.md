# d4r0

`d4r0` is a local-only game-translation overlay for Windows 11 SDR, borderless, and windowed games. 

## Build on Windows 11

Requires Visual Studio 2022 with **Desktop development with C++**, Windows 11 SDK 10.0.22621+, CMake 3.24+, Ninja, and a current AMD driver. Install CMake and Ninja with `winget install Kitware.CMake Ninja-build.Ninja` if needed.

Open **x64 Native Tools Command Prompt for VS 2022**, then configure, build, and test:

```bat
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out --parallel
ctest --test-dir out --output-on-failure
```

Run `out\\d4r0.exe` normally (not as Administrator unless the game is elevated):

```bat
.\\out\\d4r0.exe
```

On first launch, d4r0 writes `%LOCALAPPDATA%\\d4r0\\settings.ini`. It defaults to TranslateGemma 4B and never switches models automatically. The repository development build auto-discovers the pinned files under ignored `local-assets/`. Otherwise set the local paths there:

```ini
llamaExecutable=C:\\tools\\llama.cpp\\llama-server.exe
model4b=D:\\models\\translategemma-4b-q4.gguf
model12b=D:\\models\\translategemma-12b-q4.gguf
ocrDetector=C:\\models\\ppocr-det\\inference.onnx
ocrRecognizer=C:\\models\\ppocr-latin-rec\\inference.onnx
ocrDictionary=C:\\models\\ppocr-latin-rec\\inference.yml
```

`Ctrl+Shift+Tab` toggles translated-overlay mode. `Ctrl+Alt+O` immediately shows the original screen while keeping prior translations cached. `Ctrl+Alt+D` toggles local performance diagnostics and `Ctrl+Alt+Q` exits cleanly. The first three shortcuts are configurable in `settings.ini`.

Stable screen changes are grouped into bounded 960x540 OCR cores with overlap,
then translated in numbered batches. The default `maxOcrBatch=16` covers a 4K
viewport in one scheduling pass on the verified machine; `maxTranslationBatch`
remains capped at 16 items per model request. The model is health-checked and
warm-tested before d4r0 reports that translation is ready. Dense pages can show
completed groups while a later group is still processing; an item that fails
translation validation remains untranslated and is retried later.

Replay uses the Windows hardware H.264 encoder to maintain twenty private 30-second, 1080p/30 source-frame segments. It never receives overlay/text data, refuses software encoding, and deletes its temporary ring on clean exit; the next launch removes rings left by dead processes.

Elevated games can prevent a normal desktop overlay from appearing above them. HDR, exclusive fullscreen, injection, cloud translation, and anti-cheat circumvention are intentionally unsupported in v1.

