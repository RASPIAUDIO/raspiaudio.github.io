# ESPHome Source For ZeroCore S3 / Voice DSP+

Entry point: `config/zerocore-s3.yaml`. This is the source exported for the
`0.1.66-web-beta` App Store binary, not the general internal development tree.
All local components, models, UI sounds and XMOS binary payloads it references
are included. No private repository, Wi-Fi credentials or capture server is
needed to build it.

## Build

Use Python 3.11 or newer and ESPHome 2026.8.1. From this directory:

```sh
python -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements.txt
python -m esphome config config/zerocore-s3.yaml
python -m esphome compile config/zerocore-s3.yaml
```

On PowerShell, replace the activation command with
`.\.venv\Scripts\Activate.ps1`, or invoke `.\.venv\Scripts\python.exe` directly.
Use a short checkout/build path on Windows. If ESPHome cannot install unrelated
toolchains, the release build used this tools filter:

```powershell
$env:ESPHOME_IDF_DEFAULT_TOOLS_FORCE='xtensa-esp-elf;cmake;ninja;idf-exe;ccache;esp-rom-elfs'
$env:ESPHOME_BUILD_PATH='C:\VDSPBuild'
python -m esphome compile config/zerocore-s3.yaml
```

ESPHome emits `firmware.factory.bin` and `firmware.ota.bin` in its build tree.
Use the factory binary at address 0 for a fresh serial/browser installation.
Use the OTA binary only with an ESPHome OTA installation. Compilation embeds
build-time information, so another build need not have the published binary's
exact hash. The pinned ESPHome version selects the platform/framework packages;
see the version details in `../release.json`.
`esp-idf-dependencies.lock` records the resolved ESP-IDF managed components of
the published build for comparison when reproducing it.

The XMOS upgrade and SPI recovery images are inputs, not build outputs of this
ESP32 project. It does not include or rebuild the proprietary BeClear libraries.
See [UPSTREAM.md](UPSTREAM.md) for provenance and the applicable license scopes.

To change a deployed node using your local YAML and ESPHome OTA:

```sh
python -m esphome run config/zerocore-s3.yaml --device DEVICE_IP
```

Replace DEVICE_IP with your node's actual address. Rebuilding may require
matching node naming/provisioning settings; do not run upload commands when you
only intend to compile or verify this package.
