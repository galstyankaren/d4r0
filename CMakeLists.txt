cmake_minimum_required(VERSION 3.24)
project(d4r0 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(d4r0_core
  src/RegionCache.cpp
  src/TranslationPrompt.cpp
  src/Settings.cpp
)
target_include_directories(d4r0_core PUBLIC include)

if(WIN32)
  add_executable(d4r0 WIN32
    src/WinMain.cpp
    src/OverlayWindow.cpp
    src/ReplayBuffer.cpp
    src/WindowsGraphicsCapture.cpp
  )
  target_link_libraries(d4r0 PRIVATE d4r0_core d3d11 dxgi dcomp d2d1 dwrite dwmapi user32 gdi32 shell32 ole32 windowsapp)
  target_compile_definitions(d4r0 PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
else()
  message(STATUS "d4r0 is a Windows 11 application; only portable core tests are built on this host.")
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(d4r0_core_tests tests/CoreTests.cpp)
  target_link_libraries(d4r0_core_tests PRIVATE d4r0_core)
  add_test(NAME d4r0_core_tests COMMAND d4r0_core_tests)
endif()
