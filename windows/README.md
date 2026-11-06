# FalseHorizons on Windows

Everything Windows-specific lives here. The C++ code (`../src`, `../apps`, `../include`) and `../CMakeLists.txt` are shared with Linux: the Windows parts inside them are guarded (`WIN32` / `_WIN32`) and noted below.

## Build, test, install
Built and tested with MSYS2's UCRT64 toolchain (g++, CMake, Ninja). One-time, in an MSYS2 UCRT64 shell:
```sh
pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,SDL2,libwebp,pkgconf,angleproject}
```
Then, in PowerShell:
```powershell
.\build.ps1                    # build into ..\build-windows and run the tests (three open a real window for a moment)
.\build.ps1 -Install           # also install into %USERPROFILE%\FalseHorizons and add a Start menu entry
.\build.ps1 -Uninstall         # remove program, DLLs and Start menu entry; your pictures, presets and settings.json stay
```
Close the program before installing over it (the script says so). `-Prefix DIR` picks another install folder, `-Msys2 DIR` another MSYS2 location, `-SkipTests` is faster. The installed folder holds the program plus every DLL it needs and runs on a PC without MSYS2.

## How the Windows build differs
- **The program is `FalseHorizons.exe`** (CMake target and Linux binary: `falsehorizons`; `OUTPUT_NAME` capitalises it on Windows only).
- **OpenGL ES 2 is ANGLE** (`libGLESv2.dll` + `libEGL.dll`, from the `angleproject` package). There is no `glesv2.pc`, so `../CMakeLists.txt` finds the library directly. The program asks SDL for its ES context through EGL (`SDL_HINT_OPENGL_ES_DRIVER`, in `../apps/demo.cpp`) so the context and the GL calls come from the same ANGLE.
- **No offscreen testing.** ANGLE has no offscreen device, so the three render smoke tests open a real window instead of using SDL's `offscreen` driver.
- **Window program, console when asked.** The exe has no console box; started from a terminal it attaches to that one, so `--version`, `--where` and `--list-devices` print there. PowerShell does not wait for such programs: use `Start-Process -Wait` in scripts.
- **Listening:** with no `--device` it uses WASAPI loopback (whatever the PC is playing). `--list-devices` lists the microphones.
- **File names:** all paths go through `fh::path_from_utf8` / `fh::path_to_utf8` (`../shared/native/include/fh/pathutf8.hpp`), so Japanese/Chinese names work even where `path::string()` is the ANSI code page (MSVC). MinGW's libstdc++ already converts as UTF-8, so `../tests/unicode_tests.cpp` passes either way here.
- **Folder buttons** open the folder with its plain path (Explorer), not a `file://` address.
- **DLLs:** `cmake --install` copies them (and ANGLE's `libEGL.dll`, which SDL loads by name at run time) next to the exe. A bare `build-windows\FalseHorizons.exe` needs `C:\msys64\ucrt64\bin` on the PATH.
- Build output goes to `../build-windows` (git-ignored); the Linux folders `build` and `build-release` are not used here.

## Not done yet
An icon.
