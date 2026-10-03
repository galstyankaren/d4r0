# d4r0

`d4r0` is a local-only translation overlay for Windows 11 SDR, borderless, and windowed apps and games.

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

## Translation profiles

d4r0 captures the configured monitor and selects a translation profile for the
focused app on that monitor. A profile saved for an executable takes priority.
For other apps, d4r0 guesses a Game, Browser, or Professional template from
the executable name or common installation path; uncertain apps use General.
Open **Controls** from the tray to see the current app and profile, correct a
guess, or create a profile for the current app. Browser profiles apply to the
browser executable, including all its tabs.

On the **Profiles** page, choose source and target languages and edit the
profile's translation instructions. The source list is limited to languages
the installed Latin OCR recognizer can read; the target list follows the local
TranslateGemma model. The default is German to English. Save changes to apply
them to new OCR work immediately; changing the selected model still requires
a restart. Previously displayed translations are cleared when the active
profile or language pair changes.

**Generate instructions** asks the already selected local TranslateGemma model
to draft instructions from the app name, template, and optional description.
It runs only when clicked, sends no captured pixels or OCR text, and shows an
editable draft that is saved only when you choose **Save**. Prompt drafting is
best effort because TranslateGemma is trained primarily for translation. The
fixed translation format and protected-token rules remain in force when you
edit profile instructions.

`Ctrl+Shift+Tab` toggles translated-overlay mode. `Ctrl+Alt+O` immediately shows the original screen while keeping prior translations cached. `Ctrl+Alt+D` toggles local performance diagnostics and `Ctrl+Alt+Q` exits cleanly. All four shortcuts are configurable in `settings.ini`.

Open the tray menu and choose **Open controls** to change shortcuts. Choose a keyboard action or the controller toggle, click **Learn new shortcut**, then press the keys or hold controller buttons together. The detected combination appears under **Preview**. Release the controller buttons, check the preview, and click **Apply preview**. Check that **Current** changes before trying it in the game; **Cancel** leaves the current binding in place. **Use default** previews the original binding, which you can then apply. Keyboard shortcuts require Ctrl, Shift, or Alt plus a letter, digit, F1–F24, Tab, Space, or Esc. Controller combinations support gamepad buttons, shoulder buttons, paddles, and triggers. A PS4 controller exposed through a gamepad mapping uses A/B/X/Y for Cross/Circle/Square/Triangle. Windows HID controllers, including a Bluetooth DualSense that is not listed as a Windows gamepad, are learned as `Hid:N+M` button usages. You can also edit the four keyboard shortcut fields and `toggleControllerButton` in `settings.ini` while d4r0 is closed. Gamepad buttons are checked every 100 ms; HID presses are received as Windows input events. The keyboard shortcut stays active.

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

