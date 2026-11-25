import test from 'node:test';import assert from 'node:assert/strict';
import {lookupModel,profiles} from '../compatibility-data.js';
test('customer model lookup resolves groups, rejects unsupported and unknown near names',()=>{
 for(const [model,profile] of [['P1006','hp'],['P1008','hp_p1008'],['Canon LBP2900','canon'],['LBP6000','lbp6000'],['LBP6018','lbp6000'],['LBP6200','lbp6200'],['LBP6200d','lbp6200'],['LBP3010','lbp3010'],['LBP3018','lbp3010'],['LBP3050','lbp3010'],['LBP3100','lbp3010'],['LBP3108','lbp3010'],['LBP3150','lbp3010'],['LBP6030','lbp6030'],['LBP6030B','lbp6030'],['LBP6030w','lbp6030'],['MP280','pixma'],['Canon MG2500 series','pixma'],['P2015','standard']])assert.equal(lookupModel(model).profile,profile);
 for(const m of ['LBP3300','LBP6230dn','LBP151dw'])assert.equal(lookupModel(m).unsupported,true);
 for(const m of ['P1008w','LBP6040','LBP2900B','MP288','Canon G2010'])assert.equal(lookupModel(m).unknown,true);
 for(const m of ['HL-1201','1020']){const found=lookupModel(m);assert.equal(found.research,true);assert.equal(found.profile,undefined);}
 assert.equal(profiles.pixma.models.length,23);
});
test('public PIXMA model list matches actual compiled driver families',async()=>{
 const {readFile}=await import('node:fs/promises');const code=await readFile(new URL('../../components/printers/canon_pixma.c',import.meta.url),'utf8');
 const actual=[...code.matchAll(/CANON_PIXMA\(canon_\w+, "[^"]+", "([^"]+)"/g)].map(m=>'Canon '+m[1]);
 assert.deepEqual(profiles.pixma.models,actual);
});

test('public installer offers online flashing without ZIP or offline download links',async()=>{
 const {readFile}=await import('node:fs/promises');
 const html=await readFile(new URL('../index.html',import.meta.url),'utf8');
 assert.doesNotMatch(html,/href=["'][^"']*(?:\.zip|downloads\/)/i);
 assert.doesNotMatch(html,/Tải bộ nạp offline/);
 for(const profile of ['lbp6000','lbp6200','lbp3010']) {
  assert.match(html,new RegExp('option value="'+profile+'"'));
  assert.match(html,new RegExp('data-feedback-profile="'+profile+'"'));
  assert.match(profiles[profile].note,/chưa (?:thử|xác nhận)/);
 }
});
