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

On first launch, d4r0 writes `%LOCALAPPDATA%\\d4r0\\settings.ini`. It defaults to TranslateGemma 4B and never switches models automatically. Set your local runtime and model paths there:

```ini
llamaExecutable=C:\\tools\\llama.cpp\\llama-cli.exe
model4b=D:\\models\\translategemma-4b-q4.gguf
model12b=D:\\models\\translategemma-12b-q4.gguf
```

`Ctrl+Alt+T` toggles translated-overlay mode. `Ctrl+Alt+O` immediately shows the original screen while keeping prior translations cached. Elevated games can prevent a normal desktop overlay from appearing above them. HDR and exclusive fullscreen are intentionally unsupported in v1.

