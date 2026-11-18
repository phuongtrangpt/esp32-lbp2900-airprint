import test from 'node:test';import assert from 'node:assert/strict';
import {lookupModel,profiles} from '../compatibility-data.js';
test('customer model lookup resolves groups, rejects unsupported and unknown near names',()=>{
 for(const [model,profile] of [['P1006','hp'],['Canon LBP2900','canon'],['MP280','pixma'],['Canon MG2500 series','pixma'],['P2015','standard']])assert.equal(lookupModel(model).profile,profile);
 for(const m of ['LBP6030','LBP6230dn','LBP151dw'])assert.equal(lookupModel(m).unsupported,true);
 for(const m of ['LBP2900B','MP288','Canon G2010'])assert.equal(lookupModel(m).unknown,true);
 assert.equal(profiles.pixma.models.length,23);
});
test('public PIXMA model list matches actual compiled driver families',async()=>{
 const {readFile}=await import('node:fs/promises');const code=await readFile(new URL('../../components/printers/canon_pixma.c',import.meta.url),'utf8');
 const actual=[...code.matchAll(/CANON_PIXMA\(canon_\w+, "[^"]+", "([^"]+)"/g)].map(m=>'Canon '+m[1]);
 assert.deepEqual(profiles.pixma.models,actual);
});
