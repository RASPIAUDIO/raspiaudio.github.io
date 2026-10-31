# S31 - Squeezelite (experimental)

Installer: https://raspiaudio.github.io/S31/Squeezelite/

Target: **ESP32-S31-Function-CoreBoard-1, 16 MB flash / 16 MB PSRAM**.
Release `2026.10.31-4391a52` packages the MnCast application at firmware commit
`4391a52`. This is an experimental development release, not the original ESP32
MnCast release.

The firmware includes the browser line-framing fix: responses start on a new line
even when the serial console has left a prompt pending.

## New feature to test: Improv Serial

Connect using Chrome/Edge and the board's USB UART port, select **Install**, then
**Next** after installation. Wi-Fi setup uses Improv Serial on the same USB port.
Use **Join other** to enter an SSID when the cached scan is empty (particularly
when Ethernet was already connected). Passwords are sent to the board locally.
No BLE provisioning is enabled. The firmware defaults to Improv enabled, with a
switch in **System → USB Wi-Fi setup (Improv Serial)**.

The captive portal remains available. The generic access-point name and password
are both `squeezelite`, unless previously changed, at http://192.168.4.1/.

## Hardware defaults and existing settings

Fresh settings use the onboard ES8311: BCLK53, WS55, DATA56, MCLK52, SDA51,
SCL50, address 0x18, amplifier GPIO57. Ethernet uses YT8531, MDC5/MDIO6/reset7.
No external DAC or display configuration is preloaded. Existing settings on a
board with this S31 partition layout are retained, including any external DAC,
display and Sendspin pairings. Other partition layouts are not supported for
in-place migration.

Spotify Connect discovery is present but playback remains under investigation.
This release does not claim a Spotify fix or new audio-performance validation.

## Flash layout

| File | Offset | Purpose |
|---|---:|---|
| bootloader.bin | 0x2000 | S31 bootloader |
| partition-table.bin | 0x8000 | 16 MB S31 partition table |
| boot-ota0.bin | 0xd000 | Generated initial OTA selection, boots application |
| recovery.bin | 0x10000 | Recovery application |
| squeezelite.bin | 0x410000 | Main application, including Improv Serial |

The installer does **not** erase the whole flash. It does replace the partition
table and these firmware regions. No NVS or settings partition is distributed or
written. `boot-ota0.bin` is generated from sequence 1 and its Espressif CRC, not
copied from a device. SHA-256 values are in `SHA256SUMS`. Flash writes are checked
using the ESP loader's MD5 verification before installation is reported complete.
The application binary alone is for OTA/existing installations, not a blank board.

## Installer source and dependencies

The dedicated page isolates S31 support from the existing store's legacy flasher.
`installer-source/` contains ESP Web Tools 10.4.0 sources with these scoped changes:

- esptool-js **0.7.0** for ESP32-S31 detection and flashing;
- Improv Serial SDK **2.8.1**;
- S31 chip-family declaration and 16 MB minimum check;
- MD5 verification of written files;
- `keep_user_data` manifest option, preserving settings and disabling full erase;
- **Next** after installation opens Wi-Fi provisioning even when preserving data.

Rebuild from `installer-source/` with Node 24: `npm ci`, `npx tsc`, then
`npx rollup -c` with `NODE_ENV=production`. Copy `dist/web/` to `web/`.
Sources and notices are included for the web installer. Firmware development is
currently archived in the private RASPIAUDIO MNCAST_ESP31 repository; it is based on
[sle118/squeezelite-esp32](https://github.com/sle118/squeezelite-esp32).

## Validation

See `validation.json` for the precise checks and outstanding browser validation.
An actual COM21 test uses the production `flash.ts` bundled under Node with a
Web Serial adapter, followed by the official Improv SDK. This tests real hardware
and the actual JavaScript protocols; it is **not** a claim that Chrome's port
chooser and the entire visual browser flow were automated.
