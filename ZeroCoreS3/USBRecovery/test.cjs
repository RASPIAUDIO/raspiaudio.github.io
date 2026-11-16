const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const root = __dirname;
const read = name => fs.readFileSync(path.join(root,name));
const release = JSON.parse(read('release.json'));
for (const [name,info] of Object.entries(release.files)) {
  const bytes = read(name);
  assert.equal(bytes.length,info.bytes,name);
  assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'),info.sha256,name);
}
const manifest = JSON.parse(read('manifest_ZeroCoreS3_USBRecovery.json'));
assert.equal(manifest.builds[0].chipFamily,'ESP32-S3');
assert.equal(manifest.builds[0].parts[0].offset,0);
assert.equal(manifest.home_assistant_domain,undefined);
const merged = read(manifest.builds[0].parts[0].path);
for (const [file,offset] of Object.entries(release.parts_offsets)) {
  const part = read(file);
  assert.deepEqual(merged.subarray(offset,offset+part.length),part,file);
}
assert.equal(read('firmware.bin')[0],0xe9);
assert.equal(release.persistent_xmos_install,true);
assert.equal(release.qspi_written_by_bridge,true);
assert.equal(release.hardware_validated,true);
assert.equal(release.embedded_spi_sha256,'16dfca1e82a004ad7c1b0d6a4e05308a72a8c68e136fc93da6f833b615aaefad');
assert.equal(release.production_sha256,'b14a117f3077c77379773312579442706061ead5b14f8c98a5879a6ce47d7e6e');
assert.equal(crypto.createHash('sha256').update(read('source/images/production.bin')).digest('hex'),release.production_sha256);
assert.equal(release.settings_preserved_from,0x200000);
const main = read('source/src/main.cpp').toString();
assert.ok(main.includes('VOICE_DSP_V2_USB_RECOVERY_0.2.0_PERSISTENT'));
assert.ok(main.includes('factory_auto_start'));
assert.ok(main.includes('SPI_RAM_SENT_NOT_VERIFIED QSPI_unchanged'));
assert.ok(!main.includes('VOICE_DSP_V2_FACTORY_RAM_LOADER'));
const controller=read('source/src/recovery_controller.inc').toString();
assert.ok(controller.includes('INSTALL_QSPI_VERIFIED'));
assert.ok(controller.includes('no_SPI_reload'));
assert.ok(!controller.includes('factoryProgramGate'));
assert.ok(read('xmos-writer/writer.c').toString().includes('enable_winbond_quad'));
const html=fs.readFileSync(path.join(root,'../../index.html'),'utf8');
assert.ok(html.includes('label: "Voice DSP+ V2 / USB Recovery (Persistent)"'));
assert.ok(html.includes('./ZeroCoreS3/USBRecovery/USBRecovery.html'));
console.log('PASS: release hashes, merged offsets, ESP32 manifest, pinned writer/production images, persistent recovery and catalog; no hardware accessed.');
