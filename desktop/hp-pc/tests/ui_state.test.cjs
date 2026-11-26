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

test('a running PC service must not display a disconnected printer as ready',async()=>{
 const s={boot_id:'one',service_state:'Running',ready:true,settings:{esp_ip:'192.168.10.21'},events:[],device:{online:false,state:'offline',message:'USB not connected'}};
 const {get,context}=ui(s);await new Promise(setImmediate);assert.match(get('device').textContent,/Offline/);
 s.device={online:true,state:'ready',message:'Ready'};await context.refresh();assert.match(get('device').textContent,/Sẵn sàng/);
});

test('completed job clears progress and cancel, keeps history, and next job starts normally',async()=>{
 const s={boot_id:'one',ready:true,busy:true,service_state:'Running',settings:{esp_ip:'192.168.10.21'},device:{online:true,state:'ready',message:'Ready'},events:[{sequence:1,phase:'rendering',job:1}]};
 const {get,context,calls}=ui(s);await new Promise(setImmediate);
 assert.equal(get('progress').hidden,false);assert.equal(get('cancel').disabled,false);
 s.busy=false;s.events.push({sequence:2,phase:'usb-delivered',job:1,message:'USB delivered'});await context.refresh();
 assert.equal(get('progress').hidden,true);assert.equal(get('progress').value,0);assert.equal(get('cancel').disabled,true);
 assert.match(get('status').textContent,/Không có lệnh.*Sẵn sàng/);assert.match(get('log').textContent,/USB delivered/);
 await get('cancel').onclick();assert.equal(calls.filter(c=>c[0]==='cancel_job').length,0);
 s.events.push({sequence:3,phase:'media-normalized',message:'A5 matched'});await context.refresh();assert.match(get('status').textContent,/Không có lệnh/);
 s.busy=true;s.events.push({sequence:4,phase:'rendering',job:2});await context.refresh();
 assert.equal(get('progress').hidden,false);assert.equal(get('cancel').disabled,false);assert.match(get('status').textContent,/PC đang xử lý/);
 s.busy=false;s.events.push({sequence:5,phase:'failed',job:2,message:'Transfer interrupted'});await context.refresh();
 assert.equal(get('cancel').disabled,true);assert.equal(get('progress').hidden,true);assert.match(get('log').textContent,/Transfer interrupted/);
});

test('stopped or offline service cannot leave a stale cancellable job in the manager',async()=>{
 const s={boot_id:'one',ready:false,busy:true,service_state:'Stopped',settings:{esp_ip:'192.168.10.21'},events:[]};
 const {get}=ui(s);await new Promise(setImmediate);assert.equal(get('cancel').disabled,true);assert.equal(get('progress').hidden,true);assert.match(get('status').textContent,/Dịch vụ đã dừng/);
});


test('document receive progress is visible before a job exists and clears on service restart',async()=>{
 const s={boot_id:'one',ready:true,busy:false,service_state:'Running',settings:{esp_ip:'192.168.10.21'},device:{online:true,state:'ready'},events:[{sequence:1,phase:'receiving',message:'Receiving 12 MiB'}]};
 const {get,context}=ui(s);await new Promise(setImmediate);
 assert.match(get('status').textContent,/Receiving 12 MiB/);assert.equal(get('progress').hidden,false);assert.equal(get('cancel').disabled,true);
 s.boot_id='two';s.events=[];await context.refresh();assert.equal(get('progress').hidden,true);assert.equal(get('start').disabled,false);
});
