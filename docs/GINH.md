# GINH v1

All integer fields little-endian. Exactly **3833 bytes**, no trailer.

| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 4 | ASCII GINH |
| 4 | 1 | Version 1 |
| 5 | 1 | Header length 20 |
| 6 | 2 | Width 250 |
| 8 | 2 | Height 122 |
| 10 | 2 | Flags 0 |
| 12 | 4 | Payload length 3813 |
| 16 | 4 | IEEE CRC32 of payload (zlib convention) |
| 20 | 3813 | Pixels |

Pixel n=y*250+x, byte=n/8, mask=0x80>>(n%8). 1=black, 0=white. No per-row padding. Final byte low nibble must be zero. Incorrect magic/version/flags/dimensions/length/padding/CRC is rejected. Photographs bypass Glagol completely.

```
python3 -m pip install Pillow==11.3.0
python3 tools/ginh.py photo.jpg photo.ginh
```

PNG (including alpha), JPG, EXIF orientation; preserve aspect ratio with white letterbox, resize to fit 250x122, threshold 128 (override --threshold 0..255). Upload the resulting .ginh through the portal. PNG/JPG are converted on the computer, not by ESP32.

The default sleep image is extracted from the user's uploaded horizontal-run arrays, without eval or executing the uploaded program. `assets/default-sleep.ginh` is the encoded file; `src/default_sleep.h` contains only its 3813 pixel bytes in flash. No image application is interpreted at sleep.
