// Catalog and installer routing regression test; no hardware or network access.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
const html=fs.readFileSync(__dirname+'/index.html','utf8');
const definition=html.match(/const devices = (\{[\s\S]*?\n        \});/)[1];
const devices=vm.runInNewContext('('+definition+')');
const apps=devices.VoiceDSP.applications;
assert.equal(devices.ZeroCoreS3.applications.HomeAssistant,undefined);
assert.ok(devices.ZeroCoreS3.applications.USBRecovery.label.includes('Safe Recovery'));
assert.deepEqual(Object.keys(apps),['USB','I2S','HomeAssistant']);
assert.equal(new URL(apps.USB.installUrl).searchParams.get('firmware'),'v2-usb-mono-48k');
assert.equal(new URL(apps.I2S.installUrl).searchParams.get('firmware'),'v2-i2s-48k');
assert.equal(new URL(apps.HomeAssistant.installUrl).hash,'#device=ZeroCoreS3&application=VoiceDSPV2');
for(const file of [devices.VoiceDSP.image,devices.VoiceDSP.html,...Object.values(apps).map(a=>a.html)])
  assert.ok(fs.existsSync(__dirname+'/'+file),file);
const update=html.match(/function updateInstallButton\(\) \{[\s\S]*?\n        \}/)[0];
const s31={hidden:true},button={style:{},setAttribute(k,v){this[k]=v},removeAttribute(k){delete this[k]}};
const ctx=vm.createContext({devices,selectedDevice:'VoiceDSP',selectedApplication:'USB',
  externalInstallLink:{},installButton:button,document:{getElementById:()=>s31}});
vm.runInContext(update,ctx);
for(const key of Object.keys(apps)){
  ctx.selectedApplication=key;ctx.updateInstallButton();
  assert.equal(button.style.visibility,'hidden');
  assert.equal(ctx.externalInstallLink.hidden,false);
  assert.equal(ctx.externalInstallLink.href,apps[key].installUrl);
}
ctx.selectedDevice='ZeroCoreS3';ctx.selectedApplication='VoiceDSPV2';ctx.updateInstallButton();
assert.equal(button.style.visibility,'visible');assert.equal(ctx.externalInstallLink.hidden,true);
assert.equal(button.manifest,'./ZeroCoreS3/VoiceDSPV2/manifest_ZeroCoreS3_VoiceDSPV2.json');
ctx.selectedDevice='S31';ctx.updateInstallButton();
assert.equal(button.style.visibility,'hidden');assert.equal(s31.hidden,false);
ctx.selectedDevice=null;ctx.selectedApplication=null;ctx.updateInstallButton();
assert.equal(button.style.visibility,'hidden');assert.equal(ctx.externalInstallLink.hidden,true);
for(const match of html.matchAll(/<script\b([^>]*)>([\s\S]*?)<\/script>/g))
  if(!match[1].includes('src='))new vm.Script(match[2]);
console.log('PASS: 3 Voice DSP tiles, deep links, local assets, XMOS/ESP32 routing, S31 preserved, inline JS syntax');
