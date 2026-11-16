# Persistent USB Recovery 0.2.0

Separate ZeroCore application. This is not the factory tester and contains no
microphone/PAJ/button qualification gate. It installs the accepted USB 0.8.9-r4
factory image, not the experimental Safe Mode/I2S bundle.

## Architecture

1. ZeroCore loads this small writer into XMOS RAM over the existing SPI loader.
2. The XMOS starts an I2C HIL slave at 0x2c and a separate flash worker.
3. The writer checks 4 MB / 256-byte page / 4 KB sector geometry and Winbond
   JEDEC ID, preserves other status bits and enables QE for QSPI boot.
4. ZeroCore checks the embedded production SHA-256, then drives the pinned
   factory_program protocol over I2C. The lower 2 MiB are erased. Upper settings
   remain untouched. Payload blocks are read back; the boot header is last.
5. Full readback CRC must pass before XMOS is reset into QSPI boot. ZeroCore
   releases all shared pins and never sends a second USB runtime image.

The writer deliberately uses bounded Winbond SPI commands through fl_command.
On this hardware, the first candidate stalled in fl_quadEnable; a second
candidate stalled in fl_eraseSector. The command-based implementation passed
the complete erase/write/readback and USB startup on 2026-11-16 (020h).
Do not substitute the high-level calls without repeating hardware validation.

`INSTALL_QSPI_VERIFIED` is not proof of standalone cold boot. The final
qualification removes ZeroCore physically and enumerates XMOS after loss of
all power. Check the validation record before publishing.

## Build

Run `python tools/prepare_persistent_usb_recovery.py --output <new-directory>`,
then `pio run -d <new-directory>/bridge`. The builder verifies the accepted
factory hash and XFLASH analysis before composing the RAM writer. It does not
open serial/USB or flash hardware. Export with `tools/export_v2_usb_recovery.py`.

Accepted factory SHA-256:
`49172176a27d5f9bbae3f168313023af0085e2dc928f81bd716ded1dfd033566`.
Padded 950272-byte transfer SHA-256:
`b14a117f3077c77379773312579442706061ead5b14f8c98a5879a6ce47d7e6e`.
AEC, filter and production audio executable are unchanged.
