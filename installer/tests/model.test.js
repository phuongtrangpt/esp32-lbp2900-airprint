import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';
const source=readFileSync(new URL('../main.js',import.meta.url),'utf8').replace(/^import .*;$/gm,'');
function setup(search='') {
 const nodes=new Map();const pending=[];
 const document={baseURI:'https://example.test/sub/',querySelector(id){if(!nodes.has(id))nodes.set(id,{value:'',style:{},classList:{add(){},toggle(){}},addEventListener(e,f){this[e]=f}});return nodes.get(id)}};
 const context=vm.createContext({document,URL,URLSearchParams,location:{search},window:{isSecureContext:true,addEventListener(){}},navigator:{serial:{}},fetch:(url,options)=>new Promise(resolve=>pending.push({url:String(url),options,resolve}))});
 vm.runInContext(source,context);
 return {nodes,pending,context};
}
const manifest=name=>({ok:true,json:async()=>({name,version:'0.7.0',builds:[{chipFamily:'ESP32-S3'}]})});
const tick=()=>new Promise(resolve=>setImmediate(resolve));
test('HP default resolves manifest from page base; Canon query selects Canon',async()=>{
 for(const [query,profile] of [['','hp'],['?model=canon','canon']]) {
  const s=setup(query);assert.equal(s.pending[0].url,`https://example.test/sub/firmware/${profile}/web-manifest.json`);
  s.pending[0].resolve(manifest(profile));await tick();
  assert.equal(s.nodes.get('#installer').manifest,s.pending[0].url);
  assert.equal(s.nodes.get('#installer').style.display,'block');
 }
});
test('slow HP response cannot replace selected Canon manifest',async()=>{
 const s=setup();const model=s.nodes.get('#printer-model');model.value='canon';model.change();
 s.pending[1].resolve(manifest('Canon'));await tick();s.pending[0].resolve(manifest('HP'));await tick();
 assert.equal(s.nodes.get('#selected-model').textContent,'Canon');
 assert.match(s.nodes.get('#installer').manifest,/canon/);
});
test('failed selected manifest keeps install button hidden',async()=>{
 const s=setup();s.pending[0].resolve({ok:false});await tick();assert.equal(s.nodes.get('#installer').style.display,'none');
});
