"""Check release hashes, ESP32-S31 image headers and flash/NVS boundaries."""
from pathlib import Path
import binascii
import hashlib
import json
import struct

root = Path(__file__).resolve().parent
manifest = json.loads((root / 'manifest_S31_Squeezelite.json').read_text())
assert manifest['keep_user_data'] is True
assert manifest['new_install_improv_wait_time'] == 60
build = manifest['builds'][0]
assert build['chipFamily'] == 'ESP32-S31'
hashes = {name: digest for digest, name in
          (line.split() for line in (root / 'SHA256SUMS').read_text().splitlines())}
end = 0
for part in build['parts']:
    data = (root / part['path']).read_bytes()
    assert hashlib.sha256(data).hexdigest() == hashes[part['path']]
    start = part['offset']
    assert start >= end
    end = start + len(data)
    assert end <= 16 * 1024 * 1024
    for a, z in ((0x9000, 0xd000), (0xe10000, 0xe14000)):
        assert end <= a or start >= z, 'Firmware overlaps retained settings'
    if part['path'] in ('bootloader.bin', 'recovery.bin', 'squeezelite.bin'):
        assert data[0] == 0xe9 and struct.unpack_from('<H', data, 12)[0] == 32
ota = (root / 'boot-ota0.bin').read_bytes()
assert struct.unpack_from('<I', ota)[0] == 1
assert struct.unpack_from('<I', ota, 28)[0] == binascii.crc32(ota[:4], 0xffffffff)
print('PASS: hashes, S31 images, OTA0 selection, 16 MB layout and retained NVS')
