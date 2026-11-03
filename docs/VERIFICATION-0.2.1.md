# 0.2.1-alpha: battery indicator verification

Hardware testing has NOT been performed. No Wireless Paper board is attached to this workspace. Compilation and host tests do not establish electrical correctness, voltage/SOC accuracy, actual E-Ink layout or battery consumption on a device.

## Implemented

- Battery icon + approximate percentage at x190..243, baseline13 on every menu, baseline119 beside the reader page counter. Uses the existing Cyrillic 6x12 font and orientation pipeline. Photo and application canvases and sleep images have no overlay.
- Actual Wireless Paper battery circuit: GPIO19 LOW gate, GPIO20 ADC2 channel9, 10K/10K voltage divider. GPIO19 HIGH after each measurement and before sleep. Native USB CDC disabled to release GPIO19/20; board USB uses CP2102 UART.
- Nine calibrated ADC samples on the stack, median filtering, plausible-voltage checks, ADC error/saturation handling. Invalid result displays --%, never a fabricated 0%. A disconnected battery cannot reliably be detected from this circuit.
- Read on boot, before starting AP, once/minute in menus or reader with Wi-Fi off. ADC2 is not accessed while Wi-Fi runs: ~ marks the cached value. Unchanged readings do not submit a frame. ADC sampling does not extend the existing idle timeout or add a sleep wake source.
- No changes to partition table or user-file storage. Update only apps_v12 firmware.bin at 0x10000, no erase-all.

## Automated checks

Battery host tests cover curve endpoints, interpolation/monotonicity, outlier median, invalid samples, ADC failure, cache marking, Wi-Fi exclusion, polling interval/millis wraparound and worst-case text width. Existing interpreter, image/sleep coordinator, portal, converter and real LittleFS host tests remain enabled. CI builds all four firmware variants and publishes firmware/source/converter/checksums only after checks pass. See the linked Actions run for actual results.

These tests do not execute the ESP32 ADC driver or physically inspect the display. Earlier memory/storage results are in VERIFICATION-0.2.0.md.

## Physical acceptance still required

1. Boot from a known charged single-cell LiPo; compare diagnostics battery_mv to a multimeter at battery terminals (also with display load). Repeat at several charge levels; calibrate divider/curve if required.
2. Inspect every menu, errors, file deletion confirmation and book footer in both orientations. Check longest label ~100% and large page numbers do not overlap.
3. Start AP, upload files/run Wi-Fi apps; ensure cached ~ badge and no ADC reads/connection interruption. Stop AP and verify fresh reading at the next due interval.
4. Leave unchanged voltage for over a minute: no extra E-Ink update. Change percentage: one badge redraw at the next sample. Confirm the normal 120s idle sleep still occurs.
5. Confirm default/custom sleep image gets one final refresh, divider off, deep-sleep current, GPIO0-only wake, no held-button wake loop. Check serial logs through CP2102 and normal flashing.
