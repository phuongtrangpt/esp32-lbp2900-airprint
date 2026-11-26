import test from 'node:test';
import assert from 'node:assert/strict';
import { flash } from '../flash.js';
import SparkMD5 from 'spark-md5';
import { connectLoader } from '../serial.js';
globalThis.window=new EventTarget();
const manifest={version:'test',builds:[{chipFamily:'ESP32-S3',serialType:'uart',parts:[{path:'app.bin',offset:0x20000}]}]};
const port={getInfo:()=>({usbVendorId:0x10c4,usbProductId:0xea60})};
function mocks({failures=0,writeError=false,resetError=false,finishError=false,verifyError=false}={}){
  const calls=[];let attempt=0;
  class Transport {
    async disconnect(){calls.push('disconnect');}
    async setDTR(){if(resetError) throw Error('unplugged');}
    async setRTS(){}
  }
  class ESPLoader {
    constructor(options){this.options=options;}
    async main(mode){calls.push(mode);if(attempt++<failures) throw Error('sync timeout');this.chip={CHIP_NAME:'ESP32-S3'};}
    async flashId(){calls.push('flash-id');}
    async eraseFlash(){calls.push('erase');}
    async flashDeflFinish(reboot,timeout){calls.push(['finish',timeout]);if(finishError)throw Error('Serial data stream stopped');}
    async flashMd5sum(){calls.push('verify');if(verifyError)throw Error('verify timeout');return SparkMD5.ArrayBuffer.hash(new Uint8Array([1,2,3,4]));}
    async writeFlash(options){calls.push('write');if(writeError) throw Error('write timeout');options.reportProgress(0,2,4);options.reportProgress(0,4,4);await this.flashDeflFinish(false,3000);const hash=await this.flashMd5sum(0,4);if(hash!==options.calculateMD5Hash(new Uint8Array([1,2,3,4])))throw Error('MD5 mismatch');}
    async after(){calls.push('reset');}
  }
  return {Transport,ESPLoader,calls,authorize:async()=>{}};
}
for(const failures of [0,1,2]) test(`connect recovers after ${failures} failures`,async()=>{
  const deps=mocks({failures});
  await connectLoader(port,deps,()=>{});
  assert.equal(deps.calls.filter(c=>c==='disconnect').length,failures);
  assert.equal(deps.calls.includes('no_reset'),failures===2);
});
test('all connection failures release ports and preserve original error',async()=>{
  const deps=mocks({failures:3});
  await assert.rejects(connectLoader(port,deps,()=>{}),/sync timeout/);
  assert.equal(deps.calls.filter(c=>c==='disconnect').length,3);
});
test('success releases port before finished; progress reaches 100',async()=>{
  const deps=mocks();const events=[];
  globalThis.fetch=async()=>new Response(new Uint8Array([1,2,3,4]));
  await flash(e=>{events.push(e);if(e.state==='finished') assert.equal(deps.calls.at(-1),'disconnect');},port,'https://example.test/firmware/manifest.json',manifest,true,deps);
  assert.equal(events.at(-1).state,'finished');
  assert.deepEqual(events.filter(e=>e.state==='writing').map(e=>e.details.percentage),[50,99,99,99,100]);
  assert.ok(deps.calls.indexOf('erase')<deps.calls.indexOf('write'));
});
test('failed asset download never erases or writes',async()=>{
  const deps=mocks();const events=[];globalThis.fetch=async()=>new Response('missing',{status:404});
  await flash(e=>events.push(e),port,'https://example.test/manifest.json',manifest,true,deps);
  assert.equal(events.at(-1).details.error,'failed_firmware_download');
  assert.ok(!deps.calls.includes('erase')&&!deps.calls.includes('write'));
});
test('bad checksum never erases or writes',async()=>{
  const deps=mocks();const events=[];globalThis.fetch=async()=>new Response(new Uint8Array([1]));
  const invalid=structuredClone(manifest);invalid.builds[0].parts[0].sha256='bad';
  await flash(e=>events.push(e),port,'https://example.test/manifest.json',invalid,true,deps);
  assert.match(events.at(-1).message,/SHA-256/);assert.ok(!deps.calls.includes('erase'));
});
test('write and unplug errors cannot hide original error or lock the port',async()=>{
  const deps=mocks({writeError:true,resetError:true});const events=[];
  globalThis.fetch=async()=>new Response(new Uint8Array([1]));
  await flash(e=>events.push(e),port,'https://example.test/manifest.json',manifest,false,deps);
  assert.match(events.at(-1).message,/write timeout/);assert.equal(deps.calls.at(-1),'disconnect');
});
test('CDC cannot silently install firmware whose Wi-Fi setup requires UART',async()=>{
  const deps=mocks();const events=[];
  await flash(e=>events.push(e),{getInfo:()=>({usbVendorId:0x303a,usbProductId:0x1001})},'https://example.test/manifest.json',manifest,true,deps);
  assert.match(events.at(-1).message,/UART/);assert.ok(!deps.calls.includes('erase'));
});

test('final ACK waits at least 30 seconds, then verifies device flash before success',async()=>{
 const deps=mocks(),events=[];globalThis.fetch=async()=>new Response(new Uint8Array([1,2,3,4]));
 await flash(e=>events.push(e),port,'https://example.test/manifest.json',manifest,false,deps);
 assert.deepEqual(deps.calls.find(x=>Array.isArray(x)),['finish',30000]);
 assert.ok(deps.calls.indexOf('verify')<deps.calls.indexOf('reset'));
 assert.equal(events.at(-1).state,'finished');
});
for(const [key,phase] of [['finishError','finalize_failed'],['verifyError','verify_failed']])test(`${key} cannot turn 100% transmitted into success`,async()=>{
 const deps=mocks({[key]:true}),events=[];globalThis.fetch=async()=>new Response(new Uint8Array([1,2,3,4]));
 await flash(e=>events.push(e),port,'https://example.test/manifest.json',manifest,false,deps);
 assert.equal(events.at(-1).state,'error');assert.equal(events.at(-1).details.error,phase);
 assert.ok(!events.some(e=>e.state==='finished'||e.details?.percentage===100));
 assert.equal(deps.calls.filter(x=>x==='write').length,1);
});
test('MD5 used for device readback has standard known-answer vectors',()=>{
 for(const [value,expected] of [['','d41d8cd98f00b204e9800998ecf8427e'],['abc','900150983cd24fb0d6963f7d28e17f72']])assert.equal(SparkMD5.ArrayBuffer.hash(new TextEncoder().encode(value)),expected);
});

test('missing code authorization blocks serial and firmware access before flash',async()=>{
 const deps=mocks({});deps.authorize=async()=>{throw Error('Code required');};const events=[];
 await flash(e=>events.push(e),port,'https://example.test/manifest.json',manifest,false,deps);
 assert.equal(deps.calls.length,0);assert.equal(events.at(-1).state,'error');assert.match(events.at(-1).message,/Code required/);
});
