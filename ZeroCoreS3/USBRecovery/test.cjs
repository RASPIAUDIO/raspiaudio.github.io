const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const vm = require('node:vm');
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
assert.equal(release.embedded_spi_sha256,'4fd9bb38fd1112cd37164d79c35e140971c62503acc95e3a1e235c509115b439');
const main = read('source/src/main.cpp').toString();
assert.ok(main.includes('VOICE_DSP_V2_USB_RECOVERY_0.1.0'));
assert.ok(main.includes('factory_auto_start'));
assert.ok(main.includes('SPI_RAM_SENT_NOT_VERIFIED QSPI_unchanged'));
assert.ok(!main.includes('VOICE_DSP_V2_FACTORY_RAM_LOADER'));
const elements = new Map();
const context = vm.createContext({document:{querySelector(selector){
  if(!elements.has(selector))elements.set(selector,{textContent:'',addEventListener(){}});
  return elements.get(selector);
}},navigator:{},TextDecoder,TextEncoder});
vm.runInContext(read('status.js').toString(),context);
const run = code => vm.runInContext(code,context);
run("append('SPI_RAM_SENT_NOT_VERIFIED')");
assert.ok(!elements.get('#result').textContent.includes('completed'));
run("tail=''; append('VOICE_DSP_V2_USB_RECOV'); append('ERY_0.1.0\\nCOLD attempted=1 handed_off=1 outcome=SPI_RAM_SENT_NOT_VERIFIED')");
assert.match(elements.get('#result').textContent,/QSPI is not written yet/);
run("tail=''; append('COLD attempted=1 handed_off=0 outcome=BOOT_ABORT bus_busy')");
assert.match(elements.get('#result').textContent,/did not finish/);
run("tail=''; identified=false; append('VOICE_DSP_V2_FACTORY_RAM_LOADER')");
assert.match(elements.get('#result').textContent,/Factory tester still installed/);
const html=fs.readFileSync(path.join(root,'../../index.html'),'utf8');
assert.ok(html.includes('label: "Voice DSP+ V2 / USB Recovery (Beta)"'));
assert.ok(html.includes('./ZeroCoreS3/USBRecovery/USBRecovery.html'));
console.log('PASS: release hashes, merged flash offsets, ESP32-S3 manifest, exact USB SPI image, automatic loader, catalog and fragmented status parsing; no hardware accessed.');
