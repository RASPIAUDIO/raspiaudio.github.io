# Voice DSP+ on ZeroCore S3

**Beta 0.1.66-web-beta, published 2026-09-10.** This package turns ZeroCore S3
and Voice DSP+ into an ESPHome satellite for an existing Home Assistant server.
It does not install Home Assistant OS. Hardware qualification on ZeroCore is
still pending; this is not a new validated production release.

[Open the installer](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=HomeAssistant)

## Install

1. With power disconnected, attach Voice DSP+ and its microphone module to
   ZeroCore S3. Use the ESP32-S3 N16R8 configuration (16 MB flash, 8 MB octal PSRAM).
2. Connect the **ZeroCore S3 USB-C data port**, not the XMOS USB port, to a
   computer running Chrome or Edge. Select ZeroCore S3 / Home Assistant in the
   installer and click Connect. Some Windows USB bridges may require a driver.
3. Install the firmware. Erasing an existing installation removes saved Wi-Fi
   and device settings; read the installer's confirmation before proceeding.
4. Set up Wi-Fi using serial Improv or Bluetooth Improv. The fallback access
   point is `Voice DSP Plus Setup`, password `voice-dsp-plus`.
5. **Keep both boards powered after the ESP32 progress bar finishes.** The ESP32
   then checks XMOS and updates it if necessary. Allow several minutes.
6. Add the discovered `Voice DSP Plus` device to Home Assistant's ESPHome
   integration and select an Assist pipeline. Check `XMOS firmware status`:
   `PASS` is expected; `DEGRADED` or `ERROR` requires diagnostics.

No Python, ESPHome installation or XTAG is required for the browser installer.
Home Assistant and a working Assist pipeline are required for conversation.
The installer does not yet show the live XMOS update stage: use serial logs
or the Home Assistant diagnostic entities for this stage.

## What Is Included

| Part | Version / behavior |
| --- | --- |
| ESP32 application | `0.1.66-web-beta`, built with ESPHome `2026.8.1` |
| XMOS audio processor | `0.2.11`, 48 kHz I2S AUTO; upgrade and SPI recovery images embedded in the ESP32 binary |
| Microphone geometry | LINE/SQUARE selected at cold boot; change modules with power off |
| Wake word | Runs locally on ESP32; Hey Jarvis, Okay Nabu and Stop models included |
| Audio DSP | XMOS processes microphones, AEC/NS/AGC and the 12-LED ring |
| Home Assistant | Assist, media player, speaker profiles, diagnostics and optional PAJ7620U2 gestures |
| Provisioning | Improv serial/BLE and captive access point; no preconfigured home Wi-Fi |

New short synthesized RASPIAUDIO UI sounds replace prototype sound samples.
The internal benchmark UDP microphone capture is **not included**. The normal
Assist audio stream still goes to the configured Home Assistant pipeline;
its STT/TTS/cloud behavior depends on that pipeline. This beta uses the usual
unencrypted ESPHome API on the local network, so use a trusted network or rebuild
with an individual API encryption key. Never expose it directly to the Internet.

## Updates And Recovery

The factory binary contains bootloader, partition table and ESP32 application.
Flash it at offset `0`. **Do not feed it to the XMOS USB DFU loader.**
On boot the ESP32 checks the XMOS version. When needed, it performs the existing
I2C upgrade/readback flow and can use its embedded SPI-RAM recovery image.
This is not a guarantee of recovery from wiring or power faults.

Normal ESPHome OTA is available when building from [source](source/README.md).
This release does not add a hosted HTTP auto-update entity in Home Assistant.
The `.ota.bin` is an ESP32 OTA artifact, not an XMOS image and not a merged
factory image. Publishing a new manifest alone does not update installed units.

If the ESP32 cannot connect, hold IO0, tap RESET, release IO0, and select the
bootloader serial port. Reinstall from this page. If XMOS remains unavailable,
check power, the 40-pin connection and diagnostics before cycling power again.

For **standalone USB audio without an ESP32**, use the separate
[XMOS USB loader](https://raspiaudio.com/AIMIC/webflasher/).

## Release Evidence

- ESPHome configuration and compilation succeeded; no hardware was flashed for
  this publication. Real ZeroCore provisioning, audio, recovery and DOA remain
  to be checked on the assembled product.
- Hardware pin mapping was cross-checked: I2C GPIO5/6; SPI GPIO11/13/12/10;
  I2S BCLK GPIO8, LRCLK GPIO7, DOUT GPIO9, DIN GPIO15; GPIO16/MCLK unused.
- Firmware and asset hashes are in [SHA256SUMS](SHA256SUMS) and
  [release.json](release.json). The source is the source used for this build.
- The product illustration is the ZeroCore S3 image supplied by RASPIAUDIO.

See [third-party provenance and licenses](source/UPSTREAM.md). ESPHome source
and XMOS binary firmware are different licensing scopes; this is not a claim
that the XMOS BeClear implementation is open source.
