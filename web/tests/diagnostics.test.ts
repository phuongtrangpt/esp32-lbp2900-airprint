import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';
const source=readFileSync(new URL('../../src/debug.c',import.meta.url),'utf8');
const block=source.match(/static const char diagnostics_page\[\]\s*=([\s\S]*?);\n\nstatic int report_info/)![1];
const html=[...block.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(m=>JSON.parse(m[0])).join('');
const script=html.match(/<script>([\s\S]*)<\/script>/)![1];
test('diagnostic script refreshes, pauses, reports errors and recovers',async()=>{
 const elements:any={report:{textContent:''},status:{textContent:''},refresh:{onclick:null},auto:{checked:true}};
 let interval:any,requests=0,fail=false;
 const text='CAPT: USB read failed\n<img src=x onerror=evil()>\n';
 const context={document:{getElementById:(id:string)=>elements[id]},
   fetch:async(url:string,options:any)=>{assert.equal(url,'/diagnostics.txt');assert.equal(options.cache,'no-store');requests++;return {ok:!fail,status:fail?503:200,text:async()=>text};},
   setInterval:(fn:any,delay:number)=>{assert.equal(delay,3000);interval=fn;},Date};
 runInNewContext(script,context);
 const tick=()=>new Promise(r=>setImmediate(r)); await tick();
 assert.equal(elements.report.textContent,text);assert.match(elements.status.textContent,/Cập nhật/);
 interval();await tick();assert.equal(requests,2);
 elements.auto.checked=false;interval();await tick();assert.equal(requests,2);
 fail=true;elements.refresh.onclick();await tick();assert.match(elements.status.textContent,/HTTP 503/);
 fail=false;elements.refresh.onclick();await tick();assert.match(elements.status.textContent,/Cập nhật/);
 assert(html.includes("href='/diagnostics.txt'"));assert(html.includes("href='/diagnostics/coredump'"));
});
