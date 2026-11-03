# Verification and limits — 0.2.0-alpha

## Hardware status

**No real ESP32-S3 or physical E-Ink was available. Hardware tests have NOT been performed.** No measured ESP32 peak heap, sleep current, physical refresh count or GPIO0 wake result is claimed. Compilation is not a hardware test.

## Memory investigation

Original `Op` stored a full Value and an owning string per instruction. Host sizeof(Op) was 112 bytes. The compiler retained tokens for the entire source and copied its Program member on return. VM::stop only cleared vectors, retaining their capacity. A new program could be compiled while the preceding program's capacity still occupied heap.

Separately, every entry into upload mode and every application `сайт` call registered new HTTP handlers. Arduino-ESP32 2.0.17 WebServer::on allocates FunctionRequestHandler and appends it to the linked list; stop() does not delete this list. New firmware registers routes once and changes a single HTML value, freed on exit.

Menu limit was 16, not four. Eight 10KiB files and five photos fit into the unchanged LittleFS partition in the host LittleFS test. The exact reason for the reported *four* on that particular physical board cannot be established without its heap/file logs; memory pressure/retained bytecode and accumulating HTTP handlers are concrete defects, not a proved per-file four-item limit.

Measured on the supplied compact-logo app using a host C++ global-new tracker (requested allocation sizes, excluding malloc headers, fragmentation and stacks):

| Version | sizeof(Op) | Peak app heap above baseline, 8 sequential runs | Retained after stop |
|---|---:|---:|---:|
| 0.1.1 | 112 | 201510 bytes | 54448 bytes |
| New implementation | 16 | 24614 bytes | 0 bytes |

These numbers are **host measurements, not ESP32 measurements**. The tracker source is `test/memory_profile.cpp`; run it against the original supplied .inh to reproduce this fixture. Ordinary examples have separate CI-generated results. Tests inject 180 single allocation failures into compile/run, stop, and verify no retained app allocations. This does not prove that arbitrary programs can never exhaust device RAM. Oversize/complex programs are rejected; low-heap admission checks and bad_alloc recovery leave the firmware alive.

Compiler now keeps source line views, performs a count pass, allocates exactly the compact bytecode size, interns names, and moves the result. VM releases code/name/stack capacity on stop. Per-instruction memory accounting no longer allocates std::set nodes. Successful literal require checks no longer allocate temporary strings.

On the board, USB Serial emits `HEAP compile-before`, `compile-after`, `run-start`; `/diagnostics` exposes free heap, largest block, minimum heap **since boot** and phase boundary samples. This is NOT a per-application high-water mark. Minimum-since-boot includes other tasks/Wi-Fi and can reveal a new boot-wide low-water mark during compilation. Capture telemetry with tools/device_check.py and USB Serial before claiming ESP32 peak usage.

## Executed on host

- Core pagination/restart/random invalid UTF-8, interpreter expressions/arrays/loops/errors and stop, button wake/hold/triple tests.
- Actual portal JavaScript with controlled network and XML DOM doubles: INH/TXT/FB2/GINH transfers, UTF-8 and case normalization, chunk offsets, limits, confirmation/cancel.
- Native GINH decoder with header/CRC/length/truncation/padding corruption, path traversal rejection.
- Real LittleFS C implementation through littlefs-python on a 0x4e0000-byte emulated flash: eight distinct 10240-byte valid apps and five GINH, unmount/remount, byte comparison, sequential native Glagol execution, atomic replacement, individual removal and wipe. Cancellation in this storage test performs no FS call; actual portal cancel is separately tested in JavaScript. ESP32 filesystem wrapper and menu button interactions are not emulated.
- Shared refresh dedup predicate and sleep coordinator: final frame acknowledged, no readiness while BUSY/held, 80 ms stable release before readiness. The E-Ink driver and EXT0 peripheral are not emulated.
- PNG/JPG conversion and bit packing; 180 allocation-failure recovery points.

## Pending hardware acceptance

Use `tools/device_check.py` over the device upload AP. It uploads/readbacks eight 10KiB apps and five photos, runs each three times, logs actual heap telemetry, checks rejected corrupt photo and unconfirmed deletion, then removes only `_test_*` fixtures. It does not wipe user files or exercise physical sleep automatically. Requires USB Serial capture and manual observation of frame sequence, held-button release, wake on GPIO0, selected photo persistence across reset, power-off/peripheral behavior and deep-sleep current measurement. Test the on-device confirm/cancel menus separately. Use a previously backed-up device; no hardware results are supplied in this release.
