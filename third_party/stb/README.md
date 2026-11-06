# stb_image

`stb_image.h` is Sean Barrett's single-file image decoder, stb_image 2.30 (one small local change, see the end of this file), from https://github.com/nothings/stb
(commit 013ac3beddff3dbffafd5177e7972067cd2b5083, 2024-05-31). Public domain (Unlicense) or MIT, see the end of the file.
FalseHorizon uses it (only in `src/image.cpp`) to read pictures people give it as backgrounds and artifacts: JPEG, PNG, BMP, GIF (including animated),
TGA, PSD (flattened), PIC and PNM. Not supported: WebP, AVIF, TIFF, SVG.
The pictures are untrusted input, so `src/image.cpp` checks the size, dimensions and frame count of a file before handing it to the decoder.
To update: replace the file with a newer one and run the image tests.


## Local changes
- `stb_image.h`, GIF LZW reader (search for `LOCAL CHANGE`): stops adding table entries at 4096 instead of failing with "too many codes". Without it, GIFs written with a deferred clear code lost all frames after the first failure (found with a 388-frame GIF).
