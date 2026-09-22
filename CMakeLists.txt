cmake_minimum_required(VERSION 3.24)
project(d4r0 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(d4r0_core
  src/DebugLog.cpp
  src/RegionCache.cpp
  src/RegionScheduler.cpp
  src/TranslationPrompt.cpp
  src/Settings.cpp
  src/TextGrouping.cpp
  src/Diagnostics.cpp
)
target_include_directories(d4r0_core PUBLIC include)

if(WIN32)
  add_executable(d4r0 WIN32
    src/WinMain.cpp
    src/OverlayWindow.cpp
    src/ReplayBuffer.cpp
    src/WindowsGraphicsCapture.cpp
    src/LivePipeline.cpp
    src/TrayController.cpp
  )
  target_link_libraries(d4r0 PRIVATE d4r0_core d3d11 dxgi dcomp d2d1 dwrite dwmapi user32 gdi32 shell32 ole32 psapi windowsapp mfplat mfreadwrite mfuuid)
  target_compile_definitions(d4r0 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  message(STATUS "d4r0 is a Windows 11 application; only portable core tests are built on this host.")
endif()

include(CTest)
if(WIN32)
  add_library(d4r0_translation src/LocalTranslator.cpp)
  target_include_directories(d4r0_translation PUBLIC include)
  target_compile_definitions(d4r0_translation PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(d4r0_translation PRIVATE d4r0_core winhttp bcrypt windowsapp)
  add_library(d4r0_gpu src/GpuRegions.cpp)
  target_include_directories(d4r0_gpu PUBLIC include)
  target_compile_definitions(d4r0_gpu PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(d4r0_gpu PRIVATE d3d11 d3dcompiler windowsapp)
  set(D4R0_ORT_ROOT "${CMAKE_SOURCE_DIR}/local-assets/onnxruntime-directml-1.24.4" CACHE PATH "ONNX Runtime DirectML NuGet directory")
  set(D4R0_DML_ROOT "${CMAKE_SOURCE_DIR}/local-assets/directml-1.15.4" CACHE PATH "DirectML NuGet directory")
  if(EXISTS "${D4R0_ORT_ROOT}/build/native/include/onnxruntime_cxx_api.h")
    add_library(d4r0_ocr src/OcrRecognizer.cpp)
    target_include_directories(d4r0_ocr PUBLIC include PRIVATE "${D4R0_ORT_ROOT}/build/native/include" "${D4R0_DML_ROOT}/include")
    target_compile_definitions(d4r0_ocr PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(d4r0_ocr PRIVATE "${D4R0_ORT_ROOT}/runtimes/win-x64/native/onnxruntime.lib")
    target_link_libraries(d4r0 PRIVATE d4r0_gpu d4r0_ocr d4r0_translation)
    add_custom_command(TARGET d4r0 POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${D4R0_ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll" "$<TARGET_FILE_DIR:d4r0>"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${D4R0_DML_ROOT}/bin/x64-win/DirectML.dll" "$<TARGET_FILE_DIR:d4r0>")
    if(BUILD_TESTING)
      add_executable(d4r0_live_smoke tests/LiveSmoke.cpp src/LivePipeline.cpp src/WindowsGraphicsCapture.cpp src/OverlayWindow.cpp)
      target_compile_definitions(d4r0_live_smoke PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
      target_link_libraries(d4r0_live_smoke PRIVATE d4r0_core d4r0_gpu d4r0_ocr d4r0_translation
        d3d11 dxgi dcomp d2d1 dwrite dwmapi user32 gdi32 windowsapp)
      add_executable(d4r0_ocr_smoke tests/OcrSmoke.cpp)
      target_link_libraries(d4r0_ocr_smoke PRIVATE d4r0_ocr gdi32)
      add_custom_command(TARGET d4r0_ocr_smoke POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${D4R0_ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll" "$<TARGET_FILE_DIR:d4r0_ocr_smoke>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${D4R0_DML_ROOT}/bin/x64-win/DirectML.dll" "$<TARGET_FILE_DIR:d4r0_ocr_smoke>")
      add_test(NAME d4r0_ocr_cpu COMMAND d4r0_ocr_smoke "${CMAKE_SOURCE_DIR}/local-assets/ocr-rec/inference.onnx" "${CMAKE_SOURCE_DIR}/local-assets/ocr-rec/inference.yml" "${CMAKE_SOURCE_DIR}/local-assets/ocr-det/inference.onnx")
    endif()
  else()
    message(FATAL_ERROR "Native OCR runtime is required: set D4R0_ORT_ROOT and D4R0_DML_ROOT to extracted NuGet packages")
  endif()
endif()
if(BUILD_TESTING)
  if(WIN32)
    add_executable(d4r0_translation_smoke tests/TranslationSmoke.cpp)
    target_link_libraries(d4r0_translation_smoke PRIVATE d4r0_translation windowsapp)
    add_executable(d4r0_gpu_tests tests/GpuRegionsTests.cpp)
    target_link_libraries(d4r0_gpu_tests PRIVATE d4r0_gpu d3d11 windowsapp)
    add_test(NAME d4r0_gpu_tests COMMAND d4r0_gpu_tests)
    add_executable(d4r0_capture_smoke tests/CaptureSmoke.cpp src/WindowsGraphicsCapture.cpp)
    target_compile_definitions(d4r0_capture_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(d4r0_capture_smoke PRIVATE d4r0_core d4r0_gpu d3d11 windowsapp gdi32 user32)
    # Opt-in: this needs an interactive Windows session and briefly enables WGC.
    add_executable(d4r0_replay_smoke tests/ReplaySmoke.cpp src/ReplayBuffer.cpp)
    target_compile_definitions(d4r0_replay_smoke PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(d4r0_replay_smoke PRIVATE d4r0_core d3d11 windowsapp mfplat mfreadwrite mfuuid)
    # Opt-in: this requires a hardware H.264 Media Foundation encoder.
  endif()
  add_executable(d4r0_core_tests tests/CoreTests.cpp)
  target_link_libraries(d4r0_core_tests PRIVATE d4r0_core)
  add_test(NAME d4r0_core_tests COMMAND d4r0_core_tests)
  add_executable(d4r0_text_grouping_tests tests/TextGroupingTests.cpp)
  target_link_libraries(d4r0_text_grouping_tests PRIVATE d4r0_core)
  add_test(NAME d4r0_text_grouping_tests COMMAND d4r0_text_grouping_tests)
  add_executable(d4r0_settings_tests tests/SettingsTests.cpp)
  target_link_libraries(d4r0_settings_tests PRIVATE d4r0_core)
  add_test(NAME d4r0_settings_tests COMMAND d4r0_settings_tests)
  add_executable(d4r0_diagnostics_tests tests/DiagnosticsTests.cpp)
  target_link_libraries(d4r0_diagnostics_tests PRIVATE d4r0_core)
  add_test(NAME d4r0_diagnostics_tests COMMAND d4r0_diagnostics_tests)
  add_executable(d4r0_translation_contract_tests tests/TranslationContractTests.cpp)
  target_link_libraries(d4r0_translation_contract_tests PRIVATE d4r0_core)
  add_test(NAME d4r0_translation_contract_tests COMMAND d4r0_translation_contract_tests)
endif()
