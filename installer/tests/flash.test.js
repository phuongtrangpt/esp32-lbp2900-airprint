import test from 'node:test';
import assert from 'node:assert/strict';
import { flash } from '../flash.js';
import { connectLoader } from '../serial.js';
globalThis.window=new EventTarget();
const manifest={version:'test',builds:[{chipFamily:'ESP32-S3',serialType:'uart',parts:[{path:'app.bin',offset:0x20000}]}]};
const port={getInfo:()=>({usbVendorId:0x10c4,usbProductId:0xea60})};
function mocks({failures=0,writeError=false,resetError=false}={}){
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
    async writeFlash(options){calls.push('write');if(writeError) throw Error('write timeout');options.reportProgress(0,2,4);options.reportProgress(0,4,4);}
    async after(){calls.push('reset');}
  }
  return {Transport,ESPLoader,calls};
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
  assert.deepEqual(events.filter(e=>e.state==='writing').map(e=>e.details.percentage),[50,100,100]);
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
