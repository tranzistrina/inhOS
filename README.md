# inhOS 0.2.0-alpha — Heltec Wireless Paper V1.2

Reader, Glagol applications and native GINH photographs on ESP32-S3, 250x122, one GPIO0 button. **Hardware testing not performed.** See [verification report](docs/VERIFICATION-0.2.0.md) for measured host memory reductions, test boundaries and pending device checks.

## Downloads and update

[Complete firmware/source/converter archive](downloads/inhOS-0.2.0-alpha.zip) is published by CI only after successful tests/builds. SHA-256 files are beside it and inside the archive. For V1.2 use `firmware/apps_v12/firmware.bin`.

**Updating existing inhOS 0.1.x:** write only this firmware.bin at **0x10000**, with the flash erase-all option disabled. `partitions.csv` is unchanged: 8MB flash, app 0x10000/0x300000, LittleFS 0x310000/0x4e0000. Saved books/apps/photos/NVS are preserved by this update. Do NOT use FIRST_INSTALL_0x0 for an ordinary update. Save a backup before flashing.

First installation from unrelated firmware uses the bundled merged first-install image at 0x0 and the on-device explicit format confirmation. This is separate from updating inhOS. No automatic format on mount failure.

## Single button

- Menus: one click next, double select, triple back; hold 1.3s returns Home (Reader-only build toggles upload/reader).
- Reader: one next page; triple previous; double menu. Full page index is on disk and survives sleep.
- Home: Read, Apps, Photos, Upload, Settings, Sleep.
- Photos: double opens; single in viewer next image; triple back; double in viewer opens delete confirmation.
- Settings: orientation, sleep photograph (including built-in), file manager, reset reading position.
- File manager: individual .inh/.ginh, book with indices, or all user files. Confirmation defaults to CANCEL; one click changes choice, double accepts, triple/hold cancels. File removal never requires reflashing or erase_flash. Orientation and chosen sleep image persist in NVS.

## Upload and manage

Join Wi-Fi `inhOS` / password `inhreader`, open http://192.168.4.1 in a full browser. Upload TXT/FB2, .inh, .ginh; file list and confirmed delete actions are in the same portal. FB2 text conversion is in the browser; XML formatting/images/footnotes are not preserved. One current book up to 3MiB, app source up to 32KiB (complex programs may be rejected by resource limits), native image exactly 3833 bytes. User filenames normalize to ASCII. At least eight 10KiB apps and five photos are covered by host storage tests. Only the active app is loaded into RAM. Up to 64 names per UI list page-set; wipe iterates all batches.

## Native photographs and sleep

[GINH specification and converter](docs/GINH.md). Run `python3 tools/ginh.py photo.jpg photo.ginh` after installing Pillow. Pixel payload 3813 bytes, no row padding, CRC32 header; no interpreter needed. User's supplied logo is the built-in flash bitmap, not an interpreted application.

Settings → Sleep photo chooses any valid stored GINH. Sleep drains earlier frames, queues the selected image once, waits for its completion, stops app/Wi-Fi, closes files, waits for stable GPIO0 release, powers peripherals down, disables all previous wake sources, enables only EXT0 GPIO0 LOW and enters Deep-sleep. A new press wakes it; reset/power-on also boot normally. Boot input handling ignores an already-held button until released. Idle timeout is 120 seconds outside app/upload/indexing. Display failure aborts sleep instead of powering off during an unfinished update. Actual electrical behavior is pending hardware acceptance.

## Build and test

```
pip install platformio==6.2.0
pio run -e apps_v12
./tools/test.sh
node test/portal_test.cjs
pip install littlefs-python==0.19.0 Pillow==11.3.0
python3 test/storage_test.py
python3 test/converter_test.py
```

For restricted hosts without ASan support: `SANITIZERS=undefined ./tools/test.sh`. CI uses ASan/UBSan for the core test. Profiling executable tracks host requested heap separately. Run `tools/device_check.py` only with an actual device connected to its AP; it has NOT been run on hardware here.

Reader-only and V1.1 variants remain build targets. V1.2/V1.1.1 use E0213A367; V1.1 uses LCMEN2R13EFC1. V1.0 unsupported. Old `.knigo`, battery display, full Python/CPython, classes/imports and Wi-Fi STA remain unsupported. The language is Cyrillic/Python-like, not historically exact Church Slavonic. [Language reference](docs/LANGUAGE.md).

New source implementation; no original KniGO code/images copied. Earlier binary analysis prevents calling the process a strict clean-room implementation. GPL-3.0 and [dependency notices](docs/DEPENDENCIES.md) apply.
