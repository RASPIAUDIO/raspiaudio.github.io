# ZeroCore S3 + Voice DSP+ V2

**Validated 0.6.75 r8**, exact version `0.6.75-qspi-lab-r8-ble.20261117`.
Owner-confirmed DOA, output memorization and Home Assistant timer.
AEC, filter values and XMOS payloads are unchanged.

[Install](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=VoiceDSPV2) |
[Guide](https://github.com/RASPIAUDIO/Voice-DSP-Plus/blob/V2/Home%20Assistant/README.md) |
[Source](https://github.com/RASPIAUDIO/Voice-DSP-Plus/tree/V2/Home%20Assistant/Sources/ESPHome-BLE-r8)

## Installation

1. Assemble ZeroCore S3 (16 MB / 8 MB PSRAM) and Voice DSP+ V2 with power off.
2. **Blank or USB-only XMOS:** run
   [Safe Recovery (I2S)](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=USBRecovery)
   first, complete persistent provisioning, then install this Home Assistant image.
   Already provisioned persistent I2S boards do not need this extra step.
3. Connect the ZeroCore USB data port, not XMOS. Install with desktop Chrome/Edge.
   Erasing removes Wi-Fi/preferences. Press RESET if needed, not BOOT.
4. Configure 2.4 GHz Wi-Fi with serial Improv, BLE Improv or the fallback AP
   `VoiceDSP V2 Setup` (password `voicedsp-setup`). BLE opens for ten minutes
   without saved Wi-Fi, stops after setup and stays off on configured boots.
   HA needs working Bluetooth or compatible provisioning support; VirtualBox
   does not automatically pass the host Bluetooth adapter to its VM.
5. Add ESPHome in HA, choose an Assist pipeline and wait for the audio controller.
   Choose Voice Compact / Full Speaker / Custom. Start with low main volume.

## Output memory and updates

The last output is restored from memory. Accepted changes commit after the idle/
debounce gate. Explicitly selecting a product mode still uses its preset output.
Outputs, Inputs, Assistant and Diagnostic remain on the native device page.

Routine boots resume XMOS I2S in QSPI; no RAM upload or QSPI rewrite at each boot.
**Install XMOS in Flash** is a separate explicit persistent action.
**Reload embedded XMOS firmware** is temporary RAM recovery.

HA's **Diagnostic - ESP32 + XMOS Firmware** uses `manifest-stable.json`;
Beta Firmware uses `manifest-beta.json`. Both offer r8. Checks run after network
startup and every six hours, or press **Diagnostic - Check firmware updates**.
Installation stays manual. A board already on r8 is up to date.
This ESP32 update does not change the installed XMOS; future XMOS updates need
their documented persistent installation step.

**Known limitation:** a warm OTA restart can leave the audio controller unavailable.
Disconnect all power to both boards for ten seconds, then reconnect.

## Audio and downloads

General gains stay at 0 dB and hidden; use the main volume.
**External / Full Speaker need USB-C PD on Voice DSP+**, 65 W or more recommended.
Do not feed multiple supplies through the shared header.
Full Speaker uses L woofer / R tweeter with a matched crossover and protective
capacitor. Do not select an unfiltered output for a bare tweeter.
Power Good is diagnostic, not a protection against excessive power.

- [Factory, ESP32 offset 0](voicedsp-v2-0.6.75-r8-ble.factory.bin)
- [OTA for compatible ESPHome installations](voicedsp-v2-0.6.75-r8-ble.ota.bin)
- [Checksums](SHA256SUMS), [release record](release.json)
- [Notices](https://github.com/RASPIAUDIO/Voice-DSP-Plus/blob/V2/UPSTREAM.md)

ESPHome 2026.8.1 / ESP-IDF 5.5.5. 29 software tests and 24 native output
combinations pass; no new acoustic AEC qualification is claimed. Use a trusted
LAN: unencrypted API/OTA remains enabled. Older binaries remain for rollback.
