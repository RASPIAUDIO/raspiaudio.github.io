"""Offline publication checks. --write generates release metadata after a build."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parent
FACTORY = "voice-dsp-plus-zerocore-s3-0.1.66.factory.bin"
OTA = "voice-dsp-plus-zerocore-s3-0.1.66.ota.bin"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def payloads():
    return sorted(
        [ROOT / FACTORY, ROOT / OTA, ROOT.parent / "ZeroCoreS3.png"]
        + list((ROOT / "source/xmos").glob("*.bin"))
        + list((ROOT / "source/assets").rglob("*.tflite"))
        + list((ROOT / "source/assets").rglob("*.wav"))
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "manifest_ZeroCoreS3_HomeAssistant.json").read_text())
    assert manifest["version"] == "0.1.66-web-beta"
    assert manifest["builds"] == [{"chipFamily": "ESP32-S3", "parts": [{"path": FACTORY, "offset": 0}]}]
    factory = (ROOT / FACTORY).read_bytes()
    ota = (ROOT / OTA).read_bytes()
    assert factory[0] == ota[0] == 0xE9, "ESP image magic"
    assert struct.unpack_from("<H", factory, 12)[0] == 9, "ESP32-S3 image required"
    assert factory[3] >> 4 == 4, "16 MB flash header required"
    assert len(factory) < 16 * 1024 * 1024 and len(ota) < 0x7C0000
    assert ota in factory, "Factory image must contain this OTA application"
    for blob in (ROOT / "source/xmos").glob("*.bin"):
        assert blob.read_bytes() in ota, f"XMOS payload not embedded: {blob.name}"
    provenance = json.loads((ROOT / "source/assets/wake_word/provenance.json").read_text())
    for model in provenance:
        assert digest(ROOT / "source/assets/wake_word" / (model["model"] + ".tflite")) == model["sha256"]
    for config in (ROOT / "source/config").rglob("*.yaml"):
        text = config.read_text(encoding="utf-8")
        for forbidden in ("!secret", "192.168.", "pcm_tap", "futureproof_sounds/", "udp:"):
            assert forbidden not in text, f"Unexpected private content in {config.name}"
    paths = payloads()
    entries = [{"path": Path(os.path.relpath(p, ROOT)).as_posix(),
                "bytes": p.stat().st_size, "sha256": digest(p)} for p in paths]
    sums = "".join(f"{e['sha256']}  {e['path']}\n" for e in entries)
    if args.write:
        release = {"version": "0.1.66-web-beta", "date": "2026-09-10",
                   "esphome": "2026.8.1", "esp_idf": "5.5.5",
                   "compiler": "xtensa-esp-elf GCC 14.2.0 (esp-14.2.0_20260121)",
                   "python": "3.12.10", "xmos": "0.2.11",
                   "hardware_validation": "pending; no hardware flashed for this release",
                   "factory_offset": 0, "artifacts": entries}
        (ROOT / "release.json").write_text(json.dumps(release, indent=2) + "\n", encoding="utf-8")
        (ROOT / "SHA256SUMS").write_text(sums, encoding="utf-8")
    else:
        assert (ROOT / "SHA256SUMS").read_text() == sums, "Published checksum list differs"
        assert json.loads((ROOT / "release.json").read_text())["artifacts"] == entries
    print(f"PASS: ESP32-S3 factory/OTA, 16 MB, both embedded XMOS images, models, {len(entries)} hashes and config privacy checks")


if __name__ == "__main__":
    main()
