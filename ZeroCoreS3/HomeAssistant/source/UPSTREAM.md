# Provenance And License Scopes

This ESPHome application contains RASPIAUDIO adaptations of
[FutureProofHomes/Satellite1-ESPHome](https://github.com/FutureProofHomes/Satellite1-ESPHome)
at commit `9a58961438568872f2e27ae4c2aec257ea05f97a`, plus ESPHome `2026.8.1`.
The upstream MIT (Python/YAML) and GPLv3 (C/C++ runtime) terms and notices are
preserved in `LICENSE.ESPHome-FutureProof`. RASPIAUDIO's modifications to those
parts are provided under the same respective terms. No upstream endorsement is
implied.

| Local file / component | Origin and adaptation |
| --- | --- |
| `config/zerocore-s3.yaml` | `config/satellite1.yaml`: ZeroCore identity and local package assembly |
| `packages/core_board.yaml` | `common/core_board.yaml`: ESP32-S3/PSRAM/buses, no Satellite1 HAT peripherals |
| `packages/wifi-improv-product.yaml` | `common/wifi_improv.yaml`: unprovisioned BLE plus serial/captive fallbacks |
| `packages/home_assistant.yaml` | `common/home_assistant.yaml`: ESPHome API and diagnostic entities |
| `packages/assist-product.yaml` | `common/voice_assistant.yaml`, `common/timer.yaml`: wake word, timers, state machine, ducking; benchmark capture removed |
| `packages/speaker-media-product.yaml` | `common/speaker.yaml`, `common/media_player.yaml`, `common/timer.yaml`: Voice DSP+ playback, mono channel selection and codec tone controls |
| `components/i2s_audio` | FutureProof's ESPHome I2S fork; RASPIAUDIO speaker channel selection/downmix |
| `components/voice_dsp_i2s_microphone` | `satellite1/microphone`: Voice DSP+ input, 48 to 16 kHz processed path |
| `components/voice_dsp_plus`, `xmos_spi_boot`, `xmos_firmware_manager` | RASPIAUDIO host control, PCAL/SPI recovery and I2C update integration |
| `packages/speaker-types.yaml`, `packages/gesture-control.yaml`, `packages/voice-dsp-plus-runtime.yaml` | RASPIAUDIO product configuration and diagnostics |
| `components/paj7620u2` | DFRobot initialization table at `57edc1dee3d0f95613b15eb9c1ac9241308bc917`; MIT notice in `LICENSE.DFRobot` |

Package paths in the table are relative to `config/`.

## Wake Models

The exact source URLs, pinned source revisions and SHA256 values are preserved
in `assets/wake_word/provenance.json`. Hey Jarvis and VAD come from
`esphome/micro-wake-word-models` revision
`05b65922cc433c9df13e98e32a7fe520758c837e`. Okay Nabu and Stop come from the
corresponding historical `OHF-Voice/micro-wake-word` release assets listed there.
They are byte-identical to the previously used models; this publication did not
retune them. The model license is Apache 2.0, preserved in
`assets/wake_word/LICENSE`. Credit: Kevin Ahrendt and the respective ESPHome /
Open Home Foundation contributors.

## Sounds And XMOS

`assets/raspiaudio_sounds/` contains newly synthesized RASPIAUDIO sine tones,
not FutureProof-hosted audio recordings. These tones are provided under CC0 1.0.

`xmos/` contains RASPIAUDIO Voice DSP+ XMOS firmware **in binary form** for this
hardware: v0.2.11 upgrade and SPI-RAM recovery. It is not built from
FutureProofHomes/Satellite1-XMOS. The ESPHome licenses above do not grant source
rights in XMOS/VocalFusion/BeClear. No proprietary XMOS SDK source or library
archive is included in this public export.
