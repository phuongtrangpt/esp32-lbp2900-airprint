import test from 'node:test';
import assert from 'node:assert/strict';
import {createProgress,updateProgress} from '../progress-state.js';
test('progress reflects actual events, keeps write completion distinct from final success',()=>{
 let s=updateProgress(createProgress(),{state:'initializing',message:'Kết nối'},'12:00');
 assert.equal(s.percent,null);
 for(let p=0;p<=100;p++)s=updateProgress(s,{state:'writing',message:`Ghi ${p}%`,details:{percentage:p,bytesWritten:p,bytesTotal:100}},'12:01');
 assert.equal(s.percent,100);assert.equal(s.active,true);assert.ok(s.lines.length<25);
 s=updateProgress(s,{state:'error',message:'Mất kết nối'},'12:02');assert.equal(s.active,false);assert.equal(s.lines.at(-1).error,true);
 s=updateProgress(s,{state:'initializing',message:'Thử lại'},'12:03');assert.equal(s.percent,null);assert.equal(s.lines.length,1);
 s=updateProgress(s,{state:'finished',message:'Hoàn tất'},'12:04');assert.equal(s.active,false);assert.equal(s.state,'finished');
});
test('repeated connection messages and long retries keep bounded log',()=>{
 let s=createProgress();for(let i=0;i<500;i++)s=updateProgress(s,{state:'initializing',message:`Thử ${i}`},'12:00');assert.equal(s.lines.length,120);
 s=updateProgress(s,{state:'initializing',message:'Thử 499'},'12:00');assert.equal(s.lines.length,120);
});
