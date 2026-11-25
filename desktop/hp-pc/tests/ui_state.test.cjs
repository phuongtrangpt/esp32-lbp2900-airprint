const {test}=require('node:test');
const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const vm=require('node:vm');
function ui(snapshot){
 const elements=new Map();const get=id=>{if(!elements.has(id))elements.set(id,{value:'',textContent:'',disabled:false,removeAttribute(){}});return elements.get(id);};
 const calls=[];const context={document:{getElementById:get,hidden:false},localStorage:{getItem:()=> '192.168.10.21',setItem(){}},window:{__TAURI__:{core:{invoke:async(name,args)=>{calls.push([name,args]);if(name==='get_snapshot')return snapshot;}}}},Date,Math,setInterval:()=>0};
 vm.createContext(context);vm.runInContext(fs.readFileSync(path.join(__dirname,'../ui/app.js'),'utf8'),context);
 return {get,calls,context};
}
test('manager reopening discovers the service; hiding and exiting never stop printing',async()=>{
 const s={boot_id:'one',ready:true,esp:'192.168.10.21',settings:{esp_ip:'192.168.10.21'},service_state:'Running',events:[]};
 const {get,calls}=ui(s);await new Promise(setImmediate);
 assert.equal(get('ip').value,'192.168.10.21');assert.match(get('target').textContent,/Running.*192\.168\.10\.21/);
 await get('hide').onclick();await get('quit').onclick();
 assert.deepEqual(calls.map(c=>c[0]),['get_snapshot','hide_window','quit_manager']);
});
test('IP cannot be changed while printing; existing native configuration wins over old localStorage',async()=>{
 const s={boot_id:'one',ready:true,busy:true,esp:'192.168.10.22',settings:{esp_ip:'192.168.10.22'},service_state:'Running',events:[]};
 const {get,calls,context}=ui(s);await new Promise(setImmediate);
 assert.equal(get('ip').value,'192.168.10.22');assert.equal(get('start').disabled,true);await get('start').onclick();assert.equal(calls.length,1);
 s.busy=false;await context.refresh();get('ip').value='192.168.10.23';await get('start').onclick();assert.equal(calls.find(c=>c[0]==='start_worker')[1].ip,'192.168.10.23');
});
