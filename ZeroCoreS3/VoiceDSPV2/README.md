# ZeroCore S3 + Voice DSP+ V2

**Validated Home Assistant 0.6.61**, original accepted binaries. V2 hardware only.

[Install now](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=VoiceDSPV2) |
[Simple guide](https://github.com/RASPIAUDIO/Voice-DSP-Plus/blob/V2/Home%20Assistant/README.md) |
[Sources](https://github.com/RASPIAUDIO/Voice-DSP-Plus/tree/V2/Home%20Assistant/Sources)

1. Assemble ZeroCore S3 (16 MB / 8 MB PSRAM) and Voice DSP+ V2 with power off.
2. Connect the **ZeroCore S3 USB data port**, not XMOS. Use desktop Chrome/Edge.
3. Install, press RESET if needed, then provision 2.4 GHz Wi-Fi with serial Improv.
   Alternatively use AP `VoiceDSP V2 Setup`, password `voicedsp-setup`. No BLE.
4. Add the ESPHome device in Home Assistant, select your Assist pipeline and wait
   for Audio Controller Ready. Allow at least 65 seconds for startup/mode restore.
5. Select Voice Compact / Full Speaker / Custom and Apply Product Mode.
   Check output and level before playback. Save Mode Settings remembers adjustments.

The factory image is merged for ESP32 offset **0**; the OTA file is only for
compatible ESPHome installations. Erasing removes Wi-Fi/preferences. The matching
XMOS is embedded and loaded into **RAM at each boot**; QSPI is not programmed.
No XTAG is required. The ZeroCore must remain connected for this mode.

Includes harmonized internal/external level controls (-60 to 0 dB), local Hey
Jarvis, Assist, music, gestures, LED/sound feedback and diagnostics. AEC and filter
settings are unchanged from the accepted checkpoint. No acoustic retuning or
new endurance qualification is implied by this publication.

**External / Full Speaker require USB-C PD directly on Voice DSP+**, 65 W or
more recommended. Do not feed multiple power supplies through the shared header.
Full Speaker: L woofer / R tweeter, matched crossover and protective capacitor.
Do not use unfiltered External for a bare tweeter. Power-Good remains diagnostic,
not a substitute for correct supply/load or conservative playback levels.

The Home Assistant update entity uses `manifest-stable.json`; Beta Firmware uses
`manifest-beta.json`. Both currently offer this same validated release. Checks
are automatic but **installation remains manual**. Reload embedded XMOS firmware
reloads the bundled RAM image, not arbitrary firmware or a QSPI update.

Use a trusted LAN: this accepted build retains unencrypted API/OTA and opt-in
capture diagnostics. If the audio controller remains unavailable after an update,
remove all power to both boards for 10 seconds. Do not expose ports to the Internet.

## Downloads and provenance

- [Factory](voicedsp-v2-0.6.61.factory.bin)
- [OTA](voicedsp-v2-0.6.61.ota.bin)
- [SHA256](SHA256SUMS) and [release record](release.json)
- [Source and dependency notices](https://github.com/RASPIAUDIO/Voice-DSP-Plus/blob/V2/UPSTREAM.md)

ESPHome 2026.8.1, ESP-IDF 5.5.5. Embedded XMOS SHA256:
`152334171334c9608783985ee83ed4e3daad94385dd220bc4a5884b4eaa7dc50`.
The V1 entry remains separate and unchanged. Older files are retained for recovery
but are not selected by the installer.
