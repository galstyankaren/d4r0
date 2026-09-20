# Repository Guidelines

## Project Structure & Module Organization

`d4r0` is a local German-to-English game-translation overlay targeting Windows 11 SDR windowed/borderless games.

- `include/d4r0/`: public headers and domain types.
- `src/`: portable settings, region cache, and translation prompt logic; Windows entry point, overlay, and replay seam.
- `tests/CoreTests.cpp`: portable core regression tests.
- `docs/architecture.md`: pipeline and privacy constraints; `docs/HANDOFF.md`: implementation status and remaining milestones. Read both before changing pipeline behavior.
- `CMakeLists.txt`: `d4r0_core`, Windows-only `d4r0`, and `d4r0_core_tests` targets.

Models and OCR assets are supplied externally. The executable is currently an overlay shell; capture, OCR, translation runtime, and replay encoding remain integration work.

## Build, Test, and Development Commands

Use CMake 3.24+, Ninja, and a C++20 compiler. Windows builds require Visual Studio 2022 C++ tools and Windows 11 SDK 10.0.22621+; run from the x64 Native Tools Command Prompt.

```sh
cmake -S . -B out -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build out --parallel
ctest --test-dir out --output-on-failure
```

These commands configure, compile, and run tests. On Windows, launch `.\out\d4r0.exe`. Other hosts build only the portable core and tests.

## Coding Style & Naming Conventions

Follow existing C++20 code: two-space indentation, opening braces on the same line, and the `d4r0` namespace. Use PascalCase for types and matching `.h`/`.cpp` filenames, camelCase for functions and fields, and trailing underscores for private members. Keep platform-specific code outside the portable core. No formatter or linter configuration is currently checked in; match neighboring code.

## Testing Guidelines

Tests use standard `assert` with CTest, without a third-party framework or enforced coverage threshold. Use Debug builds so assertions remain active. Extend `tests/CoreTests.cpp` for portable regressions, especially stale revisions and prompt parsing. Register additional test executables with CTest. Windows UI changes also need manual checks for hotkeys, click-through behavior, DPI changes, and capture exclusion.

## Commit & Pull Request Guidelines

History uses short imperative subjects, such as `Add local game translation overlay POC`; follow that convention. PRs should describe behavior changes, link relevant issues, report build/test results and platform, and include screenshots for visible overlay changes. Identify incomplete integrations explicitly.

## Configuration & Privacy

Configure local runtime/model paths in `%LOCALAPPDATA%\d4r0\settings.ini`. Keep model assets and secrets out of commits. Preserve local-only processing, explicit user model selection, and source-frame-only replay deleted on exit. Game injection and anti-cheat circumvention are out of scope.
