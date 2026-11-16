import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile,stat } from 'node:fs/promises';
import { createHash } from 'node:crypto';
for (const profile of ['hp','canon']) {
const root=new URL(`../public/firmware/${profile}/`,import.meta.url);
const available=await stat(new URL('web-manifest.json',root)).then(()=>true,()=>false);
test(`${profile} package: SHA256, S3 image headers, offsets, bounds and NVS gap`,{skip:!available},async()=>{
 const manifest=JSON.parse(await readFile(new URL('web-manifest.json',root),'utf8'));
 assert.equal(manifest.builds.length,1);const build=manifest.builds[0];
 assert.equal(build.chipFamily,'ESP32-S3');assert.equal(build.serialType,'uart');
 assert.deepEqual(build.parts.map(p=>p.offset),[0,0xf000,0x20000]);
 for(const part of build.parts){
  assert.match(part.path,/^[\w.-]+$/);
  const data=await readFile(new URL(part.path,root));
  assert.equal(createHash('sha256').update(data).digest('hex'),part.sha256);
  assert.ok(part.offset+data.length<=0x1000000);
  assert.ok(part.offset+data.length<=0x9000 || part.offset>=0xf000);
  if(part.offset===0 || part.offset===0x20000){assert.equal(data[0],0xe9);assert.equal(data.readUInt16LE(12),9);}
 }
});

}
