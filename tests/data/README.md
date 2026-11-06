# Test pictures

Tiny pictures for `tests/image_tests.cpp`, made with Python's Pillow:

- `halves.*`: 8x4, left half red (255,0,0), right half blue (0,0,255), as PNG, BMP, JPEG (quality 95, no subsampling), TGA, PPM and GIF.
- `clear.png`: 8x4, left half fully transparent green, right half opaque green.
- `rgb.gif`: 6x6, three frames (red, green, blue), delays 50, 100 and 200 ms, looping.
- `wide.png`: 2000x500 gradient, for the shrinking checks.
- `manyframes.gif`: 400 frames of 4x4, 20 ms each (over the 300-frame limit).
- `huge-claim.png`: a valid PNG header that claims 9000 x 9000 pixels (over the 64 million pixel limit), with almost no data: it must be refused, not allocated.
- `huger-claim.png`: the same claiming 40000 x 40000 (the decoder library refuses this one itself).
- `notpicture.txt`: text.
- `orient1.jpg` .. `orient8.jpg`: the same 16x32 picture (red above blue, a green block at the top left) stored turned each of the 8 ways a camera can, with the EXIF orientation note that says how to turn it upright.
- `halves.webp`, `clear.webp` (lossless, with transparency), `clear-lossy.webp` (lossy, with transparency), `rgb.webp` (animated, the same three frames and delays as `rgb.gif`), `wide.webp`, `manyframes.webp` (400 frames), `huge-claim.webp` (a 34-byte lossless file that claims 16383 x 16383 pixels): the same pictures as WebP. Pillow with libwebp; used only when the program is built with libwebp.
