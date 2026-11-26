import test from 'node:test';
import assert from 'node:assert/strict';
test('access is memory-only and fresh import/reload requires a new code',async()=>{
 const previous=globalThis.fetch;const calls=[];
 globalThis.fetch=async(url,options)=>{calls.push([url,JSON.parse(options.body)]);return Response.json(url.endsWith('redeem')?{token:'abc'}:{ok:true});};
 try{const a=await import('../access-session.js?tab=one');assert.equal(a.hasAccess(),false);await assert.rejects(a.authorizeFlash());await a.redeemCode('code');assert.equal(a.hasAccess(),true);await a.authorizeFlash();assert.equal(calls[1][1].token,'abc');const b=await import('../access-session.js?tab=two');assert.equal(b.hasAccess(),false);await assert.rejects(b.authorizeFlash());}finally{globalThis.fetch=previous;}
});
