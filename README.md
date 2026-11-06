# FalseHorizons (C++)

A MilkDrop-style music visualizer, written in C++20. There is also a JavaScript/WebGL version of the same thing in `js/`. Presets are small `.fh-preset.json` files whose formulas run in our own safe expression language: a preset from anyone
can be run without trusting it (no calls, no allocation, no loops, hard size limits).

## Status

| Part | State |
|---|---|
| Equation language (`include/fh/expr.hpp`) | **Done.** Compiles formulas to a small stack program. Held to the JavaScript reference by 344 recorded formulas (random and hand-written, 150 of them refused with the same error message). About 3.5x faster than the JavaScript version on the same formula. |
| Preset loading (`include/fh/preset.hpp`, `json.hpp`) | **Done.** Strict JSON reader plus the same checks as the JavaScript `parsePresetFile`: ranges, text cleaning, list limits, every formula compiled. Held to the JavaScript by 2,275 recorded files (hand-written, every built-in, damaged at random). |
| Flash guard (`flashguard.hpp`) and audio analysis (`audio.hpp`: bass/mid/treble, beat, waveform, 64 bars) | **Done.** Held to the JavaScript by 501 recorded runs; the guard is also checked against the WCAG 2.3.1 "3 flashes a second" rule with a strobe at every speed. |
| Running a preset (`runtime.hpp`): the warp mesh, built-in and custom waves, shapes, colour settings, and mixing two presets | **Done.** Held to the JavaScript by 217 recorded runs (every built-in in several setups, hand-written corner cases, damaged presets and blends). Makes the numbers only; drawing is the renderer's job. |
| Renderer (`renderer.hpp`, OpenGL ES 2) and the `falsehorizons` window (SDL2) | **First version works.** Same feedback loop as the WebGL version: warp mesh, waves and shapes, echo/gamma/hue/solarize, flash guard gain and brightness measuring. Runs all 17 built-in presets on a made-up song. Plus the fractal warp, the settings panel and drawing smaller on a slow machine. Not ported on purpose: the web version's built-in vaporwave scene (replaced by the user's own backgrounds and artifacts), and album art (not for the standalone). |
| Audio capture (`capture.hpp`, miniaudio) and the analyser (`analyser.hpp`: FFT, Blackman window, smoothing, dB to bytes, like a browser's AnalyserNode) | **Works.** Listens to what the computer plays (Linux: the PulseAudio/PipeWire monitor; Windows: WASAPI loopback, works, 2026-10-10). Checked here with a test tone through a silent virtual output: bass, mid and treble came out right. |
| Backgrounds and artifacts (`image.hpp`, `scene.hpp`, `userfiles.hpp`, stb_image) | **Works.** Black by default. People give it their own pictures: JPEG, PNG, WebP (still and animated), BMP, GIF (animated too), TGA, PSD, PIC, PNM. A *background* is mixed into the feedback loop so the visuals warp it; *artifacts* float around, breathe with the bass and are smeared by the loop the same way. Untrusted files are size-, dimension- and frame-checked before decoding and fuzzed under the sanitizers. WebP needs libwebp (found at build time; without it everything else still works). Not supported: AVIF, TIFF, SVG. |
| Fractal warp (`fractal.hpp`) | **Done.** The slowly morphing Mandelbrot height field that bends the picture (Geometry in the web version, "Fractal" here). Held to the JavaScript by 67 recorded cases. Writing the port found a bug in the reference (a waveform shorter than 256 points gave a mesh of NaN; the page never does that); fixed in both. |
| Preset picker (`picker.hpp`) | **Done.** Random but never one of the last four, Previous steps back, Next retraces. Held to the JavaScript by 40 recorded runs. |
| `.milk` importer (`milk.hpp`) | **Done.** Translates MilkDrop presets you bring (none bundled); refuses loops, memory arrays and bitwise operators; ignores custom shaders; says what it lost. Drop a `.milk` file into `presets/`. Held to the JavaScript by 239 recorded files (including damaged ones). |
| Settings (`settings.hpp`) and the panel (`apps/panel.cpp`, Dear ImGui) | **Done.** Tab opens a panel: motion, fractal, background and artifact strength and on/off, calm mode, change by itself and how often, preset list with likes, buttons that open the folders. Kept in `settings.json` next to `presets/` (a plain file; a damaged or hand-edited one gives defaults for what is wrong). The settings both versions share are held to the JavaScript by 159 recorded cases. |
| Adaptive drawing size (`adapt.hpp`) | **Done.** Slow for a while: draw at 80% of the size (down to half); fast again: grow back. Same rule as the web version. |

## Build and test

```sh
cmake -S . -B build -G Ninja -DFH_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

No dependencies for the core and the tests (the demo needs SDL2 and GLES2; audio capture, picture reading and the panel use the vendored miniaudio, stb_image and Dear ImGui; WebP uses libwebp if it is installed). `FH_SANITIZE=ON` runs the tests under AddressSanitizer and UBSan.

## Install it (runs from anywhere)

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
cmake --install build-release --prefix ~/FalseHorizons     # the program plus the presets/, backgrounds/ and artifacts/ folders
```
Your pictures go in `~/FalseHorizons/backgrounds/` and `artifacts/`. Installing again updates the program and leaves your files alone.

## Windows

Everything for the Windows version (build script, install, notes on ANGLE, console, file names) is in [`windows/`](windows/README.md): `windows\build.ps1 -Install`.

## Try it

```sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release     # needs SDL2 and GLES2 development files (pacman -S sdl2)
cmake --build build-release           # (the folder `build` above is the sanitizer build for the tests: it is several times slower, do not run the window from it)
./build-release/falsehorizons                       # a window reacting to what the computer is playing; Tab opens the settings panel; N/P change preset, L like, C calm, F full screen, A real sound / demo song, Esc quits
./build-release/falsehorizons --list-devices        # what it can listen to; --device NAME picks one; --fake uses a made-up song
./build-release/falsehorizons --preset "Slow Tide" --size 1280x720
./build-release/falsehorizons --shot 200 out.ppm    # run 200 frames, save the window as a picture, quit (works without a display: SDL_VIDEODRIVER=offscreen)
```

The presets are in `presets/` (written by `node tools/export_presets.js` from the JavaScript version's built-ins). Drop any `*.fh-preset.json` or
MilkDrop `.milk` file in and it is picked up while the program runs (a `.milk` file is translated, with a note saying what was lost; `--presets DIR` points
it at another folder). Your choices are kept in `settings.json` next to the folders (`--settings FILE` uses another file).

## Your own pictures

By default the picture is black: just the preset's own waves and shapes. There are two folders next to `presets/`:

- **`backgrounds/`**: put a picture or an animated GIF in here and it becomes the background; the visuals smear and warp it. With several, it
  changes every 20 seconds (`B` for the next one).
- **`artifacts/`**: put pictures or GIFs in here and they float around on top, tilt a little, swell with the bass and jump on a beat (up to 12).
  Pictures with see-through parts (PNG, GIF) look best.

Move or copy a file in to use it, take it out to stop using it. The program notices within a couple of seconds, so it can stay running.
Formats: JPEG, PNG, WebP, BMP, GIF (animated too), TGA, PSD, PIC, PNM. Phone photos are turned upright. Big pictures are shrunk to fit, and an animation with more than 300 frames is thinned (every other frame, or every third...) so it keeps its speed but fits in memory. Pictures load in the background, so the window opens at once and they fade in as they are ready. `1` and `2` hide and show the backgrounds
and artifacts. `falsehorizons --where` prints where the three folders are; `--folder DIR` uses another root (with `presets/`, `backgrounds/`, `artifacts/`
inside), and `--presets`, `--backgrounds`, `--artifacts` point at single folders. Your pictures are not part of the project (`.gitignore` keeps them out).

## How the equation language is tested

`FalseHorizons/js/falsehorizon-expr.js` is the reference. `tests/parity/gen_expr_golden.js` runs it over a list of formulas and records everything it does
(`expr.golden`). `tests/expr_tests.cpp` runs the same formulas through the C++ version and requires the same error message, the same variables
(to 1e-9), and the same names read and written. `cmake --build build --target parity-golden` records it again (needs Node). Presets work the same way: `tests/parity/gen_preset_golden.js` writes `preset.golden`, `tests/preset_tests.cpp` checks it. The flash guard and audio analysis: `gen_hearing_golden.js` / `hearing_tests.cpp`; running presets: `gen_runtime_golden.js` / `runtime_tests.cpp`; the fractal: `gen_fractal_golden.js` / `fractal_tests.cpp`; the `.milk` importer: `gen_milk_golden.js` / `milk_tests.cpp`; the picker and settings: `gen_state_golden.js` / `state_tests.cpp`.

Writing the port found a bug in the reference: `a % b` worked out `a` twice, so a chain of `%` doubled the work at every step (24 in a row took 235 ms,
40 would have taken hours). Both versions now work it out once.

## Security

Sound and picture files, presets and settings files are treated as untrusted input: the readers check sizes and limits, damaged and random files are fed through them under the
AddressSanitizer and UBSan builds (`ctest` runs them), and the program never opens a network connection.
To report a problem, see [`SECURITY.md`](SECURITY.md).

## Licence

This is **source-available for non-commercial use**, under the PolyForm Noncommercial License 1.0.0 (the `LICENSE` file; https://polyformproject.org/licenses/noncommercial/1.0.0). You may use, change and share it for free for any non-commercial purpose; you may not sell it or use it commercially. It is not an OSI "open source" licence, because it forbids commercial use. Code from other projects that ships inside (for example in `third_party/` or `shared/native/third_party/`) keeps its own licence.
