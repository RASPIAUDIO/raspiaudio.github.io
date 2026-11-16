# Voice DSP+ V2 USB Recovery (Persistent)

[Install on ZeroCore S3](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=USBRecovery)
| [Read installation status](https://apps.raspiaudio.com/ZeroCoreS3/USBRecovery/status.html)

Restore standalone USB audio, even with a blank or damaged XMOS boot image.
No Home Assistant, Wi-Fi credentials or XTAG. Recovery **0.2.0** installs the
accepted USB mono 48 kHz **0.8.9-r4** firmware permanently. Unlike the old 0.1.0,
it does not require a second DFU upgrade or leave USB running only from RAM.

## Install once, then remove ZeroCore

1. Stop playback and disconnect speakers. With all power off, fit ZeroCore S3
   to the Voice DSP+ V2 40-pin header. Connect ZeroCore USB data to the PC.
2. Install this app, choosing erase for a clean ESP32 installation. This
   replaces Home Assistant/the factory tester on ZeroCore. Close the installer.
3. Disconnect **all power for ten seconds**, then connect both USB data ports
   to the PC. Installation starts automatically and takes several minutes.
   If ESP32 stays in download mode, press RESET once without BOOT before it starts.
4. Open the status page and select ZeroCore. Wait for `INSTALL_QSPI_VERIFIED`.
   **Do not unplug or reset during erase, write or verification.**
5. Power off both boards, physically remove ZeroCore from the header, then
   connect only Voice DSP+ USB data. Check standalone USB detection and volume
   before reconnecting speakers.

Subsequent updates can use the [XMOS USB updater](https://raspiaudio.com/voicedsp/webflasher/?firmware=v2-usb-mono-48k).
Keep the ZeroCore recovery app only if you need a provisioning fixture; it can
attempt installation again after reset. The factory tester is a separate app.

## What changes

- Replaces the lower 2 MiB XMOS boot partition with the complete accepted USB image.
- Preserves the upper 2 MiB settings area; does not change AEC/filter algorithms.
- Checks the embedded SHA-256, each written block and full image CRC on readback.
- Commits the boot header after payload transfer and block readback.
- Resets XMOS into QSPI boot, without reloading its USB application through SPI.
- Refuses unknown flash geometry and stops after an uncertain write/failure.

`SPI_RAM_SENT_NOT_VERIFIED` only reports transfer of the small RAM writer.
`INSTALL_QSPI_VERIFIED` confirms flash readback, not an independent cold boot.
That final check requires the power-off/removal step above. If installation
fails, keep the log; do not assume the old boot firmware is still intact.

## Source and build

`source/` is the complete ESP32 PlatformIO project, including both embedded
images. Build using `pio run -d source` with Espressif32 6.12.0 / Arduino 2.0.17.
`xmos-writer/` contains the small RAM writer, public XMOS I2C HIL dependency and
target definition. With the XTC 15.3.1 environment configured, build there with:

```sh
xcc -O2 -g -mcmodel=large target.xn -I. main.xc writer.c i2c_slave.c -lquadflash -o recovery-writer.xe
```

The SPI image embedded in `source/images/factory.bin` is this writer, not the
audio firmware. `production.bin` is the pinned USB factory image with trailing
FF alignment padding. `release.json` and `SHA256SUMS` record the shipped hashes
and hardware-validation status. No credentials or private test captures ship.

RASPIAUDIO bridge/writer code is MIT. XMOS HIL files retain their XMOS Public
Licence; Arduino/framework licenses remain unchanged. The accepted XMOS audio
binary is not MIT; its corresponding source package and notices are in the
[V2 USB directory](https://github.com/RASPIAUDIO/Voice-DSP-Plus/tree/V2/USB).
