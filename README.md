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

Debug capture starts **off on every launch**. Pressing the translation toggle `Ctrl+Shift+Tab` currently enables it automatically for crash diagnosis. You can also use `Ctrl+Alt+D` or the tray's debug control. It saves source/overlay PNG pairs and `events.jsonl` in `%LOCALAPPDATA%\d4r0\diagnostics\<session>`. Turn it off to stop writing. These files can include private screen pixels and text; inspect or delete the diagnostics directory in File Explorer when finished. Completed old sessions are pruned to keep the directory near 2 GB. They are independent of replay and are never uploaded.

For an OCR miss or bad panel, enable debug, reproduce the scene for several seconds, then disable it. Share the relevant session directory if you want the captured source pixels and decisions analyzed; a camera photo alone cannot show what OCR received.

To rerun OCR on a captured session without a live game, use:

```bat
.\out\d4r0_diagnostic_replay.exe "%LOCALAPPDATA%\d4r0\diagnostics\<session>" "<detector.onnx>" "<recognizer.onnx>" "<dictionary.yml>"
```

Add `--half-screen` to replay the current live crop geometry: two full-width,
overlapping OCR strips per saved frame. Add `--translate "<llama-server.exe>"
"<model.gguf>"` after that option to rerun the local translation model too. The
command writes `offline-ocr.jsonl` next to the captured images, including crop,
OCR, group, and optional translation records. OCR records include a `confirmed`
field from the same text-stability rule used by the live worker; replay visits
the captured images in numeric order. Optional model replay translates each
unique recognized group once and records its result for every frame. Without `--half-screen`, it
replays logged crops when their frame revision matches a saved PNG. Match image
IDs and revisions to `events.jsonl` when comparing results.

Changed tiles trigger OCR of their full-width half-screen strip, with a 64 px
vertical overlap and a 2048 px detector input on 4K screens. This keeps long
article lines intact. Matching recognition on a later pass confirms the text;
long scrolling lines can also confirm through an overlapping shifted substring.
the scheduler also samples moving areas so animation cannot defer OCR forever.
`maxTranslationBatch` remains capped at 16 items per model request. The model
is health-checked and warm-tested before d4r0 reports that translation is
ready. Dense pages can show one completed strip while the other is processing.

Replay uses the Windows hardware H.264 encoder to maintain twenty private 30-second, 1080p/30 source-frame segments. It never receives overlay/text data, refuses software encoding, and deletes its temporary ring on clean exit; the next launch removes rings left by dead processes.

Elevated games can prevent a normal desktop overlay from appearing above them. HDR, exclusive fullscreen, injection, cloud translation, and anti-cheat circumvention are intentionally unsupported in v1.

