import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile,stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
for (const profile of ['hp','hp_p1008','canon','lbp6000','lbp6200','lbp3010','lbp6030','pixma','standard']) {
const root=new URL(`../public/firmware/${profile}/`,import.meta.url);
const available=await stat(new URL('web-manifest.json',root)).then(()=>true,()=>false);
test(`${profile} package: SHA256, S3 image headers, offsets, bounds and NVS gap`,{skip:!available},async()=>{
 const manifest=JSON.parse(await readFile(new URL('web-manifest.json',root),'utf8'));
 if(profile==='hp_p1008')assert.equal(manifest.hpBootstrapEmbedded,true);
 assert.equal(manifest.builds.length,1);const build=manifest.builds[0];
 assert.equal(build.chipFamily,'ESP32-S3');assert.equal(build.serialType,'uart');
 if(['lbp6030','lbp6000','lbp6200','lbp3010'].includes(profile)){
  assert.equal(manifest.usbOnly,true);
  await assert.rejects(stat(new URL('ota-manifest.json',root)),{code:'ENOENT'});
 }else{
  const ota=JSON.parse(await readFile(new URL('ota-manifest.json',root),'utf8'));
  assert.equal(ota.profile,profile);assert.equal(ota.version,manifest.version);assert.equal(ota.file,'firmware.bin');
  const app=await readFile(new URL(ota.file,root));assert.equal(ota.size,app.length);assert.equal(ota.sha256,createHash('sha256').update(app).digest('hex'));
 }
 assert.deepEqual(build.parts.map(p=>p.offset),[0,0xf000,0x20000]);
 for(const part of build.parts){
  assert.match(part.path,/^[\w.-]+$/);
  const data=await readFile(new URL(part.path,root));
  assert.equal(createHash('sha256').update(data).digest('hex'),part.sha256);
  assert.ok(part.offset+data.length<=0x1000000);
  assert.ok(part.offset+data.length<=0x9000 || part.offset>=0xf000);
  if(part.offset===0x20000){assert.equal(data.readUInt32LE(32),0xabcd5432);assert.equal(data.subarray(80,112).toString().split('\0')[0],`vietphuong_${profile}`);}
  if(part.offset===0 || part.offset===0x20000){assert.equal(data[0],0xe9);assert.equal(data.readUInt16LE(12),9);}
 }
});

}
