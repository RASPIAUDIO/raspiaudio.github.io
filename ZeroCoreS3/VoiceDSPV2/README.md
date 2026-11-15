# ZeroCore S3 + Voice DSP+ V2

**Validated Home Assistant 0.6.75**, exact accepted binaries. V2 hardware only.

[Install now](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=VoiceDSPV2) |
[Simple guide](https://github.com/RASPIAUDIO/Voice-DSP-Plus/blob/V2/Home%20Assistant/README.md) |
[Sources](https://github.com/RASPIAUDIO/Voice-DSP-Plus/tree/V2/Home%20Assistant/Sources)

1. Assemble ZeroCore S3 (16 MB / 8 MB PSRAM) and Voice DSP+ V2 with power off.
2. Connect the **ZeroCore S3 USB data port**, not XMOS. Use desktop Chrome/Edge.
3. Install, press RESET if needed, then provision 2.4 GHz Wi-Fi with serial Improv.
   Alternatively use AP `VoiceDSP V2 Setup`, password `voicedsp-setup`. No BLE.
4. Add the ESPHome device in Home Assistant, select your Assist pipeline and wait
   for Diagnostic - XMOS firmware status: Ready. Allow at least 65 seconds for startup.
5. Select Voice Compact / Full Speaker / Custom; changes apply when audio is idle
   and settings are saved automatically. Start with a low main volume.

The factory image is merged for ESP32 offset **0**; the OTA file is only for
compatible ESPHome installations. Erasing removes Wi-Fi/preferences. The matching
XMOS is embedded and loaded into **RAM at each boot**; QSPI is not programmed.
No XTAG is required. The ZeroCore must remain connected for this mode.

One native device page, no extra dashboard: Outputs, Inputs, Assistant and
Diagnostic prefixes. Audio Output is in Controls; PAJ Motion and USB-C PD contract,
voltage/current/power are in Sensors. PAJ Motion indicates a recent gesture, not
occupancy; PD Power is available contract power, not measured consumption.
Internal/external general gains are 0 dB, hidden by default; main volume remains
adjustable. Local wake words, Assist, gestures, LEDs/sounds and diagnostics remain.
AEC, XMOS and filter tuning are unchanged. No new acoustic qualification is claimed.

**External / Full Speaker require USB-C PD directly on Voice DSP+**, 65 W or
more recommended. Do not feed multiple power supplies through the shared header.
Full Speaker: L woofer / R tweeter, matched crossover and protective capacitor.
Do not use unfiltered External for a bare tweeter. Power-Good remains diagnostic,
not a substitute for correct supply/load or conservative playback levels.

The Home Assistant update entity uses `manifest-stable.json`; Beta Firmware uses
`manifest-beta.json`. Both currently offer this same validated release. Checks
run after startup networking and every six hours, or via Check firmware updates;
**installation remains manual**. An XMOS change is delivered in a new paired ESP32
package/version, not a separate update entity. Reload embedded XMOS firmware
reloads the bundled RAM image, not arbitrary firmware or a QSPI update.

Use a trusted LAN: this accepted build retains unencrypted API/OTA and opt-in
capture diagnostics. If the audio controller remains unavailable after an update,
remove all power to both boards for 10 seconds. Do not expose ports to the Internet.

## Downloads and provenance

- [Factory](voicedsp-v2-0.6.75.factory.bin)
- [OTA](voicedsp-v2-0.6.75.ota.bin)
- [SHA256](SHA256SUMS) and [release record](release.json)
- [Source and dependency notices](https://github.com/RASPIAUDIO/Voice-DSP-Plus/blob/V2/UPSTREAM.md)

ESPHome 2026.8.1, ESP-IDF 5.5.5. Embedded XMOS SHA256:
`207b73b9ead1e84769ca4b8b208917ab2c5086fcd809b16edca11e224759c2bc`.
The V1 entry remains separate and unchanged. Older files are retained for recovery
but are not selected by the installer.
