# CoreZero S3 Digital Radio installer

[Install from apps.raspiaudio.com](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=DigitalRadio)

This release installs the [ZeroCore S3 Digital Radio application](https://github.com/RASPIAUDIOadmin/Digital-Radio-for-Raspberry-Pi/tree/main/esp32/zerocore_s3_digital_radio) for the RASPIAUDIO SI4689 40-pin shield. The factory image combines the ESP32-S3 bootloader, the ZeroCore partition table, OTA initialization, and the FM/DAB+ web firmware. Its partition table reserves `radio_cfg` at `0x7C0000` for Wi-Fi settings. The image contains no local Wi-Fi credentials.

**Factory installation replaces the existing ESP32 firmware and settings.** Select the installer's erase option for a clean installation. This package is for a ZeroCore S3 with 16 MB flash and the Digital Radio shield; it is not for the Raspberry Pi or the original ESP32 radio prototype.

After installation, join the `RASPIAUDIO-Radio-XXXXXX` hotspot using password `raspiaudio` and open `http://192.168.4.1/`. The local page can save a 2.4 GHz network SSID/password in device flash and returns to hotspot mode if connection fails. Audio plays on the shield's analog outputs, not in the browser.

## Build and verification

- Version: `0.1.0-web-beta`.
- Firmware source: [`Digital-Radio-for-Raspberry-Pi` commit `18e44eb`](https://github.com/RASPIAUDIOadmin/Digital-Radio-for-Raspberry-Pi/commit/18e44eb45387a093bd942b8c36c276ef04a7e8c4), PlatformIO environment `zerocore_s3_web`.
- `corezero-digital-radio-0.1.0.factory.bin` was assembled with `esptool --chip esp32s3 merge_bin` from the PlatformIO bootloader at `0x0`, partition table at `0x8000`, `boot_app0.bin` at `0xE000`, and the radio application at `0x10000`; flash mode DIO, 80 MHz, 16 MB.
- The source application was validated on an assembled ZeroCore S3 + shield with FM/DAB tuning, analog audio, local web controls, Wi-Fi persistence and hotspot fallback. The merged factory image was checked against its components and SHA-256 digest; it was not flashed over the preserved `app0` of that prototype.

See [`SHA256SUMS`](SHA256SUMS) and the [GPIO and serial guide](https://github.com/RASPIAUDIOadmin/Digital-Radio-for-Raspberry-Pi/tree/main/esp32/zerocore_s3_digital_radio).
