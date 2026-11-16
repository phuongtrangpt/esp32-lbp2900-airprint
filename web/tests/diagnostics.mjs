// Exercise the real C-embedded diagnostic page in desktop/mobile Chromium.
import { chromium } from 'playwright-core';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createServer } from 'node:http';
const source=readFileSync(new URL('../../src/debug.c',import.meta.url),'utf8');
const block=source.match(/static const char diagnostics_page\[\]\s*=([\s\S]*?);\n\nstatic int report_info/)[1];
const html=[...block.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(m=>JSON.parse(m[0])).join('');
let reads=0,fail=false;
const text='Firmware: test\nInternal RAM: 12345\nCAPT: USB read failed\n<img src=x onerror="throw Error(1)">\n';
const server=createServer((req,res)=>{
 if(req.url==='/diagnostics.txt'){
  reads++;res.writeHead(fail?503:200,{'Content-Type':'text/plain; charset=utf-8','Content-Disposition':'attachment; filename=printserver-log.txt','Cache-Control':'no-store'});res.end(fail?'Unavailable':text);
 }else{res.writeHead(200,{'Content-Type':'text/html; charset=utf-8'});res.end(html);}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const url=`http://127.0.0.1:${server.address().port}/diagnostics`;
const browser=await chromium.launch({...(process.env.CHROME_BIN?{executablePath:process.env.CHROME_BIN}:{channel:'chrome'}),headless:true,args:['--no-sandbox']});
try{
 for(const width of [1280,375]){
  fail=false;
  const page=await browser.newPage({viewport:{width,height:850}}),errors=[];
  page.on('pageerror',e=>errors.push(e.message));
  await page.goto(url);
  await page.waitForFunction(()=>document.getElementById('report').textContent.includes('CAPT: USB read failed'));
  assert.equal(await page.locator('#report img').count(),0,'log is plain text, no HTML execution');
  assert.match(await page.locator('#status').textContent(),/Cập nhật/);
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth),'no horizontal mobile overflow');
  const before=reads;
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Cập nhật'));
  await new Promise(r=>setTimeout(r,3200));assert(reads>before,'automatic refresh');
  await page.locator('#auto').uncheck();
  const downloadPromise=page.waitForEvent('download');
  await page.getByRole('link',{name:'Tải báo cáo lỗi (.txt)'}).click();
  const download=await downloadPromise;assert.equal(download.suggestedFilename(),'printserver-log.txt');
  fail=true;await page.locator('#refresh').click();
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('HTTP 503'));
  fail=false;await page.locator('#refresh').click();
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Cập nhật'));
  assert.deepEqual(errors,[]);await page.close();
 }
 console.log('Diagnostics UI: desktop/mobile, auto/manual refresh, text-only logs, download, error/recovery passed');
}finally{await browser.close();server.close();}
