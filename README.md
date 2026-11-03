# inhOS 0.3.0-alpha — Heltec Wireless Paper V1.2

Reader, Glagol applications and native GINH photographs on ESP32-S3, 250x122, one GPIO0 button. **Hardware testing not performed.** See [verification report](docs/VERIFICATION-0.3.0.md) for USB/battery test boundaries and pending device checks; [0.2.0 report](docs/VERIFICATION-0.2.0.md) contains host memory measurements.

## Downloads and update

[Complete firmware/source/converter archive](downloads/inhOS-0.3.0-alpha.zip) is published by CI only after successful tests/builds. SHA-256 files are beside it and inside the archive. For V1.2 use `firmware/apps_v12/firmware.bin`.

**Updating existing inhOS 0.1.x / 0.2.x:** write only this firmware.bin at **0x10000**, with the flash erase-all option disabled. `partitions.csv` is unchanged: 8MB flash, app 0x10000/0x300000, LittleFS 0x310000/0x4e0000. Saved books/apps/photos/NVS are preserved by this update. Do NOT use FIRST_INSTALL_0x0 for an ordinary update. Save a backup before flashing.

First installation from unrelated firmware uses the bundled merged first-install image at 0x0 and the on-device explicit format confirmation. This is separate from updating inhOS. No automatic format on mount failure.

## Single button

- Menus: one click next, double select, triple back; hold 1.3s returns Home (Reader-only build toggles upload/reader).
- Reader: one next page; triple previous; double menu. Full page index is on disk and survives sleep.
- Home: Read, Apps, Photos, Upload, Settings, Sleep.
- Photos: double opens; single in viewer next image; triple back; double in viewer opens delete confirmation.
- Settings: orientation, sleep photograph (including built-in), file manager, reset reading position.
- File manager: individual .inh/.ginh, book with indices, or all user files. Confirmation defaults to CANCEL; one click changes choice, double accepts, triple/hold cancels. File removal never requires reflashing or erase_flash. Orientation and chosen sleep image persist in NVS.

## USB file manager (0.3.0)

[Русская инструкция и готовые клиенты](desktop/README-RU.md), [INHFS/1 protocol](docs/INHFS-1.md). On the device select Settings → USB-файлы, then open the Windows/macOS client. CP2102 UART0, 115200 baud, native USB CDC disabled. Wi-Fi/app/indexing stop during the session; Deep-sleep is inhibited. Triple-click or hold exits. USB mode survives a reset on port open; after an established connection has no valid requests for 60 seconds the USB session closes and the device returns to normal mode. File transfers time out after 15 seconds without valid packets. GUI Disconnect returns to normal mode. Existing file data is replaced atomically only after validation; enough free space for the entire temporary copy is required.

## Battery indicator

Every device menu has a battery icon and percentage at the top right. The reader has it on the page-counter line. Measurement runs once at boot, before Wi-Fi starts, and at most once per minute in menus/reader; unchanged percentages do not refresh E-Ink or reset the sleep timer. Photos, sleep images and app graphics remain unmodified. With Wi-Fi active the last measurement is marked `~`; `--%` means no valid measurement. The single-cell LiPo voltage curve gives an approximate percentage, affected by load, temperature and USB charging. It does not detect charging or reliably detect an absent battery.

Wireless Paper uses GPIO19 LOW to enable its 10K/10K divider, GPIO20/ADC2 channel 9 to read it, then GPIO19 HIGH to disable it. ADC2 is not sampled with Wi-Fi running. Native USB CDC is disabled because GPIO19/20 are used by the battery circuit; the board's CP2102 USB-UART is used for serial logs. Calibration uses the ESP-IDF ADC calibration API; the divider/percentage accuracy still needs measurement on a physical board. [Heltec schematic](https://resource.heltec.cn/download/Wireless_Paper/Wireless_Paper_V0.4_Schematic_Diagram.pdf), [board pin definitions](https://github.com/adafruit/circuitpython/blob/main/ports/espressif/boards/heltec_wireless_paper/pins.c).

`http://192.168.4.1/diagnostics` reports `battery_mv`, `battery_percent` (null on failure), `battery_age_ms` and `battery_cached_wifi`; querying it does not force an ADC reading or interrupt Wi-Fi.

## Upload and manage

Join Wi-Fi `inhOS` / password `inhreader`, open http://192.168.4.1 in a full browser. Upload TXT/FB2, .inh, .ginh; file list and confirmed delete actions are in the same portal. FB2 text conversion is in the browser; XML formatting/images/footnotes are not preserved. One current book up to 3MiB, app source up to 32KiB (complex programs may be rejected by resource limits), native image exactly 3833 bytes. User filenames normalize to ASCII. At least eight 10KiB apps and five photos are covered by host storage tests. Only the active app is loaded into RAM. Upload checks UTF-8/NUL/size in 256-byte blocks and does not compile apps; syntax/resource errors are reported on launch. Up to 64 names per UI list page-set; wipe iterates all batches.

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

Reader-only and V1.1 variants remain build targets. V1.2/V1.1.1 use E0213A367; V1.1 uses LCMEN2R13EFC1. V1.0 unsupported. Old `.knigo`, full Python/CPython, classes/imports and Wi-Fi STA remain unsupported. The language is Cyrillic/Python-like, not historically exact Church Slavonic. [Language reference](docs/LANGUAGE.md).

New source implementation; no original KniGO code/images copied. Earlier binary analysis prevents calling the process a strict clean-room implementation. GPL-3.0 and [dependency notices](docs/DEPENDENCIES.md) apply.
