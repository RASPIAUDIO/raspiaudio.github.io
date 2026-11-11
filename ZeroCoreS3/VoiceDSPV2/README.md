# ZeroCore S3 + Voice DSP+ V2

**0.6.53-web-beta.20261111, V2 hardware only.**

[Open the installer](https://raspiaudio.github.io/#device=ZeroCoreS3&application=VoiceDSPV2).
Select ZeroCore S3, then Voice DSP+ V2 / Assistant (Beta).
The V1 entry remains separate and unchanged.

## One ESP32 image, both processors

Connect the ZeroCore S3 USB data port and install the factory image. It contains
the ESP32 bootloader, partitions, ESPHome application and matched XMOS SPI image.
Flash the factory `.bin` at **offset 0**. The `.ota.bin` is for ESPHome OTA only.
Neither file belongs in the XMOS USB DFU loader.

XMOS is loaded into RAM by the ESP32 at each startup, before I2S/audio starts.
No XTAG and no separate manual XMOS upload are required. XMOS QSPI is not written:
the shield will not boot this image independently without the ZeroCore.

Requires ZeroCore S3 with 16 MB flash and 8 MB octal PSRAM, Voice DSP+ V2 shield,
matching 40-pin connection, adequate power and a Home Assistant server.
Do not install on Voice DSP+ V1 or FutureProof Satellite1 hardware.

## First connection

1. Install using desktop Chrome or Edge. Erase only if you want to remove stored settings.
2. If the ESP32 remains in download mode, tap RESET once without holding BOOT.
3. Configure 2.4 GHz Wi-Fi using serial Improv in the installer. No personal Wi-Fi credentials are built in. BLE provisioning is not included.
4. Serial provisioning fallback: use the captive AP `VoiceDSP V2 Setup`, password `voicedsp-setup`, on a trusted local network.
5. Add the ESPHome device `voicedsp-v2-xxxxxx` to Home Assistant and assign an Assist pipeline. Each board gets its own MAC-based suffix.
6. Check `Audio Controller Ready` and `XMOS SPI Boot`. Select the output in `Sortie audio`, starting at low volume.

Native ESPHome API and OTA are intended for a trusted LAN; this development
build does not configure API encryption or an OTA password. Do not expose them
to the Internet. The development microphone capture server is absent.

## Features and limits

- Local Hey Jarvis runs on ESP32; XMOS handles the audio processing.
- Internal stereo speakers, external speaker, jack, gestures and LED feedback.
- External speaker selects Both mono and the accepted relaxed AEC profile automatically.
- Warm-start/cold-start SPI loading was observed on the predecessor engineering build with the same XMOS payload. This web variant changes identity and removes bench controls/capture; compilation is not full hardware acceptance.
- A spontaneous SDA-low I2C lock remains unresolved. Full power removal from both boards for 10 seconds recovered one occurrence. A 90-second external-route observation passed; this is not endurance qualification.
- Power-Good/PD reporting does not inhibit audio in this build. Use appropriate power, speaker load and conservative volume; do not interpret the gain slider as a safe-power guarantee.
- Internal AEC, acoustics of new assemblies and long-term stability are not qualified here.

Installation may play the short startup cue on internal speakers. Keep the
speakers clear of your ears. No connected hardware is automatically updated
merely because this manifest is published.

## Provenance

ESPHome 2026.8.1, ESP-IDF 5.5.5; RASPIAUDIO V2 .6.52 external-mono baseline plus
.6.53 automatic SPI bootstrap. Matched XMOS payload SHA256:
`152334171334c9608783985ee83ed4e3daad94385dd220bc4a5884b4eaa7dc50`.
XMOS/VocalFusion/BeClear is distributed as a hardware-specific binary, not as
open-source DSP code. Release hashes are in `SHA256SUMS` and `release.json`.
