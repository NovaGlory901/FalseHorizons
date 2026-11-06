cmake_minimum_required(VERSION 3.20)
if(POLICY CMP0207)
  cmake_policy(SET CMP0207 NEW)     # runtime-dependency paths are matched after normalising them (Windows)
endif()
project(FalseHorizon VERSION 0.1.0 DESCRIPTION "A MilkDrop-style music visualizer, C++ port" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(FH_BUILD_TESTS "Build and register the tests" ON)
option(FH_SANITIZE "Build with the address and undefined-behaviour sanitizers (GCC/Clang)" OFF)

# With no build type given an unoptimised program is made, which is many times slower (reading a big GIF took 35 s instead of 4): so default to Release.
# (Not for a sanitizer build: that one is for finding bugs and keeps the plain flags.)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES AND NOT FH_SANITIZE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Must come before any target is made, or the libraries are built without it.
if(FH_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# MinGW: build the compiler's own runtime into the program, so fewer DLLs have to travel with it.
if(MINGW)
  add_link_options(-static-libgcc -static-libstdc++)
endif()

find_package(PkgConfig QUIET)

# Code that DejaVuAmp also uses (the JSON reader, UTF-8 paths, the sound analysis, the random picker, folder watching, miniaudio, Dear ImGui) lives in shared/native/.
# In the public copy of this program the shared code sits inside it (shared/native); in the full repository it is one folder up.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/shared/native)
  set(SHARED ${CMAKE_CURRENT_SOURCE_DIR}/shared/native)
else()
  set(SHARED ${CMAKE_CURRENT_SOURCE_DIR}/../shared/native)
endif()

# The equation language: formulas compiled to a small stack program. No dependencies, no platform code.
add_library(fh_expr src/expr.cpp)
target_include_directories(fh_expr PUBLIC include)

# Presets: a small JSON reader/writer and the checker that turns an untrusted .fh-preset.json into a safe preset.
add_library(fh_preset ${SHARED}/src/json.cpp src/preset.cpp src/milk.cpp src/library.cpp ${SHARED}/src/picker.cpp src/settings.cpp)
target_include_directories(fh_preset PUBLIC include ${SHARED}/include)
target_link_libraries(fh_preset PUBLIC fh_expr)

# Hearing: the flash guard and the audio analysis (bass/mid/treble, beat, waveform, spectrum bars).
add_library(fh_hearing src/flashguard.cpp ${SHARED}/src/audio.cpp ${SHARED}/src/analyser.cpp)
target_include_directories(fh_hearing PUBLIC include ${SHARED}/include)

# Listening to what the computer plays (miniaudio, vendored in third_party/). Its header is large, so it is compiled in this one file only.
find_package(Threads REQUIRED)
add_library(fh_capture src/capture.cpp)
target_include_directories(fh_capture PUBLIC include ${SHARED}/include)
target_include_directories(fh_capture SYSTEM PRIVATE ${SHARED}/third_party/miniaudio)
target_link_libraries(fh_capture PUBLIC fh_hearing PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

# Reading pictures people give it (backgrounds and artifacts): stb_image, vendored in third_party/, compiled in this one file only.
add_library(fh_image src/image.cpp src/scene.cpp ${SHARED}/src/userfiles.cpp)
target_include_directories(fh_image PUBLIC include ${SHARED}/include)
target_include_directories(fh_image SYSTEM PRIVATE third_party/stb)
# WebP pictures are read through libwebp when it is installed (libwebp and libwebpdemux); without it everything else still works.
option(FH_WEBP "Read WebP pictures (needs libwebp and libwebpdemux)" ON)
if(FH_WEBP AND PkgConfig_FOUND)
  pkg_check_modules(FH_WEBPLIB QUIET IMPORTED_TARGET libwebp libwebpdemux)
endif()
if(FH_WEBPLIB_FOUND)
  target_sources(fh_image PRIVATE src/webp.cpp)
  target_compile_definitions(fh_image PUBLIC FH_HAVE_WEBP)
  target_link_libraries(fh_image PUBLIC PkgConfig::FH_WEBPLIB)
  message(STATUS "WebP pictures: supported (libwebp ${FH_WEBPLIB_VERSION})")
else()
  message(STATUS "WebP pictures: not supported (libwebp and libwebpdemux not found, or FH_WEBP is off)")
endif()

# One preset running: the warp mesh, the waves and shapes to draw, the colour settings, and mixing two presets.
add_library(fh_runtime src/runtime.cpp src/fractal.cpp)
target_include_directories(fh_runtime PUBLIC include)
target_link_libraries(fh_runtime PUBLIC fh_preset)

foreach(lib fh_expr fh_preset fh_hearing fh_runtime fh_capture fh_image)
  if(MSVC)
    target_compile_options(${lib} PRIVATE /W4)
  else()
    target_compile_options(${lib} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-align
      $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor> $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
  endif()
endforeach()

# The renderer (OpenGL ES 2) and the demo window (SDL2). Skipped, with a note, if SDL2 or GLES2 development files are missing.
option(FH_BUILD_APP "Build the renderer and the falsehorizons window" ON)
if(FH_BUILD_APP)
  if(PkgConfig_FOUND)
    pkg_check_modules(FH_SDL2 QUIET IMPORTED_TARGET sdl2)
    pkg_check_modules(FH_GLES2 QUIET IMPORTED_TARGET glesv2)
  endif()
  # Windows (MSYS2 ANGLE package): the headers and libGLESv2 are there but there is no glesv2.pc, so look for them directly.
  if(NOT FH_GLES2_FOUND)
    find_path(FH_GLES2_INCLUDE GLES2/gl2.h)
    find_library(FH_GLES2_LIB NAMES GLESv2 libGLESv2)
    if(FH_GLES2_INCLUDE AND FH_GLES2_LIB)
      add_library(PkgConfig::FH_GLES2 INTERFACE IMPORTED)
      target_include_directories(PkgConfig::FH_GLES2 INTERFACE ${FH_GLES2_INCLUDE})
      target_link_libraries(PkgConfig::FH_GLES2 INTERFACE ${FH_GLES2_LIB})
      set(FH_GLES2_FOUND TRUE)
      message(STATUS "OpenGL ES 2: ${FH_GLES2_LIB}")
    endif()
  endif()
  if(FH_SDL2_FOUND AND FH_GLES2_FOUND)
    add_library(fh_renderer src/renderer.cpp)
    target_include_directories(fh_renderer PUBLIC include ${SHARED}/include)
    target_link_libraries(fh_renderer PUBLIC fh_runtime fh_image PkgConfig::FH_GLES2)
    if(MSVC)
      target_compile_options(fh_renderer PRIVATE /W4)
    else()
      target_compile_options(fh_renderer PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -Wcast-align
      $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor> $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>)
    endif()
    # Dear ImGui (vendored in shared/native/third_party/imgui) draws the settings panel; its own code is not held to this project's warning level.
    add_library(fh_imgui STATIC
      ${SHARED}/third_party/imgui/imgui.cpp ${SHARED}/third_party/imgui/imgui_draw.cpp ${SHARED}/third_party/imgui/imgui_tables.cpp ${SHARED}/third_party/imgui/imgui_widgets.cpp
      ${SHARED}/third_party/imgui/backends/imgui_impl_sdl2.cpp ${SHARED}/third_party/imgui/backends/imgui_impl_opengl3.cpp)
    target_include_directories(fh_imgui SYSTEM PUBLIC ${SHARED}/third_party/imgui)
    target_compile_definitions(fh_imgui PUBLIC IMGUI_IMPL_OPENGL_ES2)
    target_link_libraries(fh_imgui PUBLIC PkgConfig::FH_SDL2 PkgConfig::FH_GLES2 ${CMAKE_DL_LIBS})
    add_executable(falsehorizons apps/falsehorizons.cpp apps/panel.cpp)
    target_link_libraries(falsehorizons PRIVATE fh_renderer fh_capture fh_hearing fh_preset fh_imgui PkgConfig::FH_SDL2)
    if(WIN32)
      set_target_properties(falsehorizons PROPERTIES OUTPUT_NAME FalseHorizons)     # Windows: FalseHorizons.exe (Linux keeps `falsehorizons`)
    endif()
    target_compile_definitions(falsehorizons PRIVATE FH_VERSION="${PROJECT_VERSION}")
    # `cmake --install build-release --prefix ~/FalseHorizons` makes a folder that runs from anywhere (the program finds its folders next to itself, also through
    # a link in ~/.local/bin): the program, the built-in presets, and the plain folders you drop pictures in (only their README is copied, never your own files).
    if(WIN32)
      # Windows: the DLLs the program needs (SDL2, libwebp, ANGLE's libGLESv2, the thread library) go next to it, since the PC has none of them.
      # SDL loads ANGLE's libEGL.dll by name at run time, so the dependency scan cannot see it: it is added by hand.
      install(TARGETS falsehorizons RUNTIME_DEPENDENCY_SET fh_runtime_deps RUNTIME DESTINATION .)
      get_filename_component(FH_DLL_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)     # MSYS2 keeps its DLLs next to the compiler
      install(RUNTIME_DEPENDENCY_SET fh_runtime_deps DESTINATION .
        DIRECTORIES ${FH_DLL_DIR}
        PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
        POST_EXCLUDE_REGEXES ".*[/\\\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\\\].*")
      find_program(FH_EGL_DLL NAMES libEGL.dll)
      if(FH_EGL_DLL)
        install(FILES ${FH_EGL_DLL} DESTINATION .)
      else()
        message(WARNING "libEGL.dll (ANGLE) not found: an installed copy will not be able to open its window")
      endif()
    else()
      install(TARGETS falsehorizons RUNTIME DESTINATION .)
    endif()
    install(DIRECTORY presets/ DESTINATION presets FILES_MATCHING PATTERN "*.fh-preset.json")
    install(FILES backgrounds/README.txt DESTINATION backgrounds)
    install(FILES artifacts/README.txt DESTINATION artifacts)
    install(FILES README.md DESTINATION .)
    if(FH_BUILD_TESTS)
      add_test(NAME version COMMAND falsehorizons --version)
      set_tests_properties(version PROPERTIES PASS_REGULAR_EXPRESSION "^falsehorizons [0-9]+[.][0-9]+")
      # Linux: SDL's "offscreen" video driver (no screen needed). Windows: ANGLE has no offscreen device, so the tests open a real window for a second each.
      if(WIN32)
        set(FH_SMOKE_ENV "ASAN_OPTIONS=detect_leaks=0")
      else()
        set(FH_SMOKE_ENV "SDL_VIDEODRIVER=offscreen;ASAN_OPTIONS=detect_leaks=0")
      endif()
      # Draws 60 frames into a hidden window (SDL's "offscreen" video driver) and checks that nothing fails. Needs a working GL ES 2 driver.
      enable_testing()
      add_test(NAME render_smoke COMMAND falsehorizons --size 320x180 --shot 60 ${CMAKE_CURRENT_BINARY_DIR}/smoke.ppm --folder ${CMAKE_CURRENT_SOURCE_DIR})
      set_tests_properties(render_smoke PROPERTIES ENVIRONMENT "${FH_SMOKE_ENV}")
      # The same with a still background and two artifacts (one an animated GIF), from the folders in tests/smoke/.
      add_test(NAME render_scene_smoke COMMAND falsehorizons --size 320x180 --shot 90 ${CMAKE_CURRENT_BINARY_DIR}/smoke-scene.ppm --folder ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke)
      set_tests_properties(render_scene_smoke PROPERTIES ENVIRONMENT "${FH_SMOKE_ENV}")
      # The same with the settings panel open (Dear ImGui drawing over the picture).
      add_test(NAME render_panel_smoke COMMAND falsehorizons --size 640x360 --panel --shot 30 ${CMAKE_CURRENT_BINARY_DIR}/smoke-panel.ppm --folder ${CMAKE_CURRENT_SOURCE_DIR}/tests/smoke)
      set_tests_properties(render_panel_smoke PROPERTIES ENVIRONMENT "${FH_SMOKE_ENV}")
    endif()
  else()
    message(STATUS "SDL2 or GLES2 development files not found: not building the renderer and falsehorizons")
  endif()
endif()

if(FH_BUILD_TESTS)
  enable_testing()
  # One program with a group per area; each group is its own ctest entry. fuzz_test is separate
  # because it runs against time budgets.
  add_executable(fh_tests tests/main.cpp
    tests/expr_tests.cpp tests/preset_tests.cpp tests/analyser_tests.cpp tests/image_tests.cpp
    tests/userfiles_tests.cpp tests/unicode_tests.cpp tests/findroot_tests.cpp tests/scene_tests.cpp
    tests/hearing_tests.cpp tests/runtime_tests.cpp tests/fractal_tests.cpp tests/milk_tests.cpp
    tests/state_tests.cpp)
  target_link_libraries(fh_tests PRIVATE fh_expr fh_preset fh_image fh_hearing fh_runtime)
  target_include_directories(fh_tests PRIVATE ${SHARED}/include)
  target_compile_definitions(fh_tests PRIVATE
    FH_PARITY_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/parity"
    FH_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data"
    FH_PRESETS="${CMAKE_CURRENT_SOURCE_DIR}/presets")
  foreach(group expr preset analyser image userfiles unicode findroot scene hearing runtime fractal milk state)
    add_test(NAME ${group} COMMAND fh_tests ${group})
  endforeach()

  add_executable(fuzz_test tests/fuzz_test.cpp)
  target_link_libraries(fuzz_test PRIVATE fh_expr fh_preset fh_image)
  target_compile_definitions(fuzz_test PRIVATE FH_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests/data" FH_PRESETS="${CMAKE_CURRENT_SOURCE_DIR}/presets")
  add_test(NAME fuzz COMMAND fuzz_test)

  # Re-records what the JavaScript reference does (needs Node). Only needed when the reference or the input lists change.
  find_program(FH_NODE node)
  if(FH_NODE)
    add_custom_target(parity-golden
      COMMAND ${FH_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/gen_expr_golden.js
      COMMAND ${FH_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/gen_preset_golden.js
      COMMAND ${FH_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/gen_hearing_golden.js
      COMMAND ${FH_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/gen_runtime_golden.js
      COMMAND ${FH_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/gen_fractal_golden.js
      COMMAND ${FH_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/gen_milk_golden.js
      COMMAND ${FH_NODE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/parity/gen_state_golden.js
      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
      COMMENT "Recording the JavaScript reference's answers in tests/parity/*.golden")
  endif()
endif()
