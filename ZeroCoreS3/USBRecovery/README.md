# Voice DSP+ V2 Safe Recovery 0.5.7 (USB / I2S)

[Install on ZeroCore S3](https://apps.raspiaudio.com/#device=ZeroCoreS3&application=USBRecovery)
| [Installation status](https://apps.raspiaudio.com/ZeroCoreS3/USBRecovery/status.html)
| [XMOS USB / I2S updater](https://raspiaudio.com/voicedsp/webflasher/)

Provisions V2 with permanent USB Safe Mode and the accepted I2S 48 kHz UI2
application. USB recovery appears for five seconds at startup, then boots the
installed application (initially I2S). A DFU connection holds recovery open.
Invalid/missing application stays in recovery. Unlike previous 0.2.0 USB-only
recovery, this preserves a USB update path after selecting I2S.

## First installation

1. Stop playback and disconnect speakers. With all power off, mount ZeroCore
   on the V2 header and connect its USB data cable to the PC.
2. Install this app and select erase. It replaces the ZeroCore application.
3. Close the installer, cut all power for ten seconds, reconnect both USBs.
4. Read ZeroCore status. Wait for `INSTALL_QSPI_VERIFIED`; do not unplug during
   erase/write/readback. The initial SPI RAM transfer is not completion.
5. Power off, physically remove ZeroCore, power up Voice DSP+ alone. USB
   recovery appears for five seconds then I2S starts. The normal I2S host
   must configure clocks, audio and codec; USB disappearance is not an audio test.

To switch later, open the XMOS updater, power-cycle Voice DSP+ and connect to
`Voice DSP+ USB Recovery 5s` within five seconds. Select USB or I2S. Once DFU
is connected, recovery stays open throughout the update. Missing the window
only requires another power cycle. The selected application survives power loss.

The fixture remembers successful provisioning by payload CRC. For a **different
board**, erase/reinstall this ESP32 app before provisioning; a stored verified
flag is not proof that the newly attached board has been programmed.

## Scope and validation

- Lower 2 MiB of supported 4 MiB XMOS QSPI replaced; upper settings preserved.
- Payload SHA256, block readback and full CRC checked before booting QSPI.
- Exact tested ZeroCore binary, not a rebuild of audio/AEC code.
- Confirmed: standalone recovery cold boot, five-second window, I2S bootstrap
  identity and DFU from recovery to USB and back to I2S.
- One transfer from active USB audio failed. Its interrupted image recovered
  through Safe Mode. Prefer that recovery path for updates.
- No new acoustic AEC test; existing AEC/filter code and accepted I2S unchanged.
- Initial provisioning interruption can require ZeroCore again. Factory Check
  remains a separate RAM-only tester, not part of this provisioning app.

## Sources and binaries

`source/` is the ESP32 PlatformIO project (Espressif32 6.12.0 / Arduino 2.0.17).
`source/images/factory.bin` is the small SPI RAM writer, not the audio firmware.
`source/images/production.bin` is the complete Safe Mode + I2S QSPI bundle.
`xmos-writer/` contains the RAM writer and public XMOS I2C dependency. Build it
with XTC 15.3.1: `xcc -O2 -g -mcmodel=large target.xn -I. main.xc writer.c i2c_slave.c -lquadflash -o recovery-writer.xe`.
`safe-mode/` contains the custom loader and recovery hooks; these are not a
standalone replacement for the licensed XMOS SDK. Release hashes are in
`release.json` and `SHA256SUMS`. Older versioned binaries are retained only
for rollback; the installer selects 0.5.7.

RASPIAUDIO code is MIT; third-party notices remain unchanged. XMOS audio
binaries are not MIT. No credentials or private measurement captures included.
