# Build status — 0.1.0-capt-preview

ESP32-S3 N16R8 firmware compilation: **PASS**, Espressif32 6.12.0 / ESP-IDF 5.5.0.
Build target: `esp32s3`, 16 MB flash, octal PSRAM, 80 MHz flash clock.
Application image: 1,671,727 bytes of the 4 MB application slot (39.9%).
The distributed firmware.bin additionally includes a 179 KB symbol table.
No physical printer or board was attached. Hardware validation remains pending.

The environment required PLATFORMIO_CORE_DIR to point to the real cache path,
because a symlink alias caused duplicate object filenames in PlatformIO's CMake
importer. No framework source patches were needed.

Artifacts: bootloader.bin at 0x0, partitions.bin at 0x8000, ota_data_initial.bin
at 0xf000, firmware.bin at 0x20000. Flash settings are read from the generated
ESP-IDF flasher_args.json rather than inferred from board marketing names.
SHA256SUMS in the package verifies all binary images and the manifest.
