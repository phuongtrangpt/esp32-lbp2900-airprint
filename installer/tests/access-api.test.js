import test from 'node:test';
import assert from 'node:assert/strict';
import {DatabaseSync} from 'node:sqlite';
import {readFileSync} from 'node:fs';
import worker from '../public/_worker.js';
function fixture(){
 const db=new DatabaseSync(':memory:');db.exec(readFileSync(new URL('../cloudflare/schema.sql',import.meta.url),'utf8'));
 const prepare=sql=>({bind(...params){return {async first(){return db.prepare(sql).get(...params)||null;},async run(){db.prepare(sql).run(...params);return {success:true};},async execute(){return {success:true,results:db.prepare(sql).all(...params)};}};}});
 const env={KEY_DB:{prepare,async batch(statements){db.exec('BEGIN');try{const results=[];for(const s of statements)results.push(await s.execute());db.exec('COMMIT');return results;}catch(e){db.exec('ROLLBACK');throw e;}}},ASSETS:{fetch:async()=>new Response('static')}};
 return {db,env,async call(action,data={},origin='https://esp.vietphuong.io.vn'){const r=await worker.fetch(new Request('https://esp.vietphuong.io.vn/api/access/'+action,{method:'POST',headers:{Origin:origin,'Content-Type':'application/json','CF-Connecting-IP':'192.0.2.1'},body:JSON.stringify(data)}),env);return {status:r.status,headers:r.headers,data:await r.json()};}};
}
test('random codes redeem once, yield a usable session, and never cache secrets',async()=>{
 const f=fixture();try{
  const a=await f.call('new'),b=await f.call('new');assert.equal(a.status,200);assert.notEqual(a.data.code,b.data.code);assert.match(a.data.code,/^[a-f0-9]{32}$/);assert.equal(a.headers.get('cache-control'),'no-store');assert.equal(a.headers.get('set-cookie'),null);
  const one=await f.call('redeem',{code:a.data.code});assert.equal(one.status,200);assert.equal((await f.call('check',{token:one.data.token})).status,200);
  assert.equal((await f.call('redeem',{code:a.data.code})).status,409);
  assert.equal((await f.call('check',{token:'0'.repeat(32)})).status,401);
  f.db.exec('UPDATE access_sessions SET expires=0');assert.equal((await f.call('check',{token:one.data.token})).status,401);
 }finally{f.db.close();}
});
test('expired or malformed code never unlocks flashing; atomic transaction consumes exactly once',async()=>{
 const f=fixture();try{
  const {data}=await f.call('new');f.db.exec('UPDATE access_codes SET expires=0');assert.equal((await f.call('redeem',{code:data.code})).status,409);
  assert.equal((await f.call('redeem',{code:'bad'})).status,400);
  const a=await f.call('new');assert.equal((await f.call('redeem',{code:a.data.code})).status,200);assert.equal((await f.call('redeem',{code:a.data.code})).status,409);
  assert.equal(f.db.prepare('SELECT COUNT(*) AS n FROM access_sessions').get().n,1);
 }finally{f.db.close();}
});
test('cross-site code requests rejected; rate limit bounded; static firmware unaffected',async()=>{
 const f=fixture();try{
  assert.equal((await f.call('new',{},'https://evil.test')).status,403);
  for(let i=0;i<12;i++)assert.equal((await f.call('new')).status,200);
  assert.equal((await f.call('new')).status,429);
  assert.equal(await (await worker.fetch(new Request('https://esp.vietphuong.io.vn/firmware/hp/firmware.bin'),f.env)).text(),'static');
  assert.equal((await worker.fetch(new Request('https://esp.vietphuong.io.vn/api/access/new'),f.env)).status,405);
 }finally{f.db.close();}
});
test('key hostname opens code page; missing database fails closed',async()=>{
 const f=fixture();try{const r=await worker.fetch(new Request('https://key.vietphuong.io.vn/'),f.env);assert.equal(r.headers.get('location'),'https://key.vietphuong.io.vn/lay-ma/');delete f.env.KEY_DB;assert.equal((await f.call('new')).status,503);}finally{f.db.close();}
});
