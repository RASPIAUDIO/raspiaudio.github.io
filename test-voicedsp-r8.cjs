const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.join(__dirname, 'ZeroCoreS3', 'VoiceDSPV2');
const version = '0.6.75-qspi-lab-r8-ble.20261117';
const expected = {
  factory: '35e4c78c6d35cc2b5045745ba556873f8ac0ddb72c61b1c4b00be93517bbee26',
  ota: 'b0261f965f9489ac1a74441ba69cbb34f1712c97e174a57985b36d4493231552',
};
const digest = (data, algorithm = 'sha256') => crypto.createHash(algorithm).update(data).digest('hex');
const release = JSON.parse(fs.readFileSync(path.join(root, 'release.json')));
assert.equal(release.version, version);
for (const file of ['manifest-stable.json', 'manifest-beta.json', 'manifest_ZeroCoreS3_VoiceDSPV2.json']) {
  const manifest = JSON.parse(fs.readFileSync(path.join(root, file)));
  assert.equal(manifest.version, version);
  assert.equal(manifest.builds.length, 1);
  const build = manifest.builds[0];
  assert.equal(build.chipFamily, 'ESP32-S3');
  assert.equal(build.parts.length, 1);
  assert.equal(build.parts[0].offset, 0);
  const factory = fs.readFileSync(path.join(root, build.parts[0].path));
  const ota = fs.readFileSync(path.join(root, build.ota.path));
  assert.equal(digest(factory), expected.factory);
  assert.equal(digest(ota), expected.ota);
  assert.equal(digest(ota, 'md5'), build.ota.md5);
  assert.deepEqual(factory.subarray(0x10000), ota);
  assert.equal(release.files[build.parts[0].path].bytes, factory.length);
  assert.equal(release.files[build.ota.path].bytes, ota.length);
  assert.match(build.ota.release_url, /RELEASE-r8\.md$/);
  assert.match(build.ota.summary, /Recovery/);
}
const old = fs.readFileSync(path.join(root, 'voicedsp-v2-0.6.75.factory.bin'));
const current = fs.readFileSync(path.join(root, 'voicedsp-v2-0.6.75-r8-ble.factory.bin'));
assert.deepEqual(current.subarray(0x8000, 0x9000), old.subarray(0x8000, 0x9000));
const page = fs.readFileSync(path.join(root, 'VoiceDSPV2.html'), 'utf8');
assert.match(page, /application=USBRecovery/);
assert.match(page, /BLE Improv/);
assert.doesNotMatch(page, /loaded into XMOS RAM automatically at every/);
console.log('PASS r8 manifests, exact hashes, OTA/factory payload, partition compatibility and recovery guidance');
