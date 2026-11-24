import {readFileSync} from 'node:fs';
import vm from 'node:vm';
import assert from 'node:assert/strict';
function page(file,fetch){
 const source=readFileSync(file,'utf8').replaceAll('VP_FACEBOOK','"https://www.facebook.com/leviethoaiphuong"');
 const c=source.match(/static const char page\[\]\s*=\s*([\s\S]*?);\n\s*static /)[1];
 const html=[...c.matchAll(/"(?:\\.|[^"\\])*"/g)].map(x=>JSON.parse(x[0])).join('');
 const js=html.split('<script>')[1].split('</script>')[0];
 const nodes=new Map();function node(id){if(!nodes.has(id))nodes.set(id,{value:id==='mode'?'dhcp':'',textContent:'',children:[],append(x){this.children.push(x)},replaceChildren(...items){this.children=items}});return nodes.get(id)}
 const alerts=[];const context=vm.createContext({document:{getElementById:node,createElement:()=>({})},fetch,setInterval(){},confirm:()=>true,alert:x=>alerts.push(x)});vm.runInContext(js,context);return {context,node,alerts,html};
}
const tick=()=>new Promise(setImmediate);
let current={ap_name:'vietphuong esp-A123',ap:false,error:0,connected:false,pending:false,ip:''};
const sent=[];
let scans=0;
const s=page('src/setup_portal.c',async(url,options)=>{
 if(url.endsWith('/status'))return {json:async()=>current};
 if(url.endsWith('/scan')){
   scans++;
   return {ok:true,json:async()=>({networks:[{ssid:'Weak',rssi:-81,channel:1},{ssid:'<img src=x onerror=alert(1)>',rssi:-42,channel:6},{ssid:'Weak',rssi:-50,channel:11},{ssid:'5 GHz',rssi:-25,channel:36}]})};
 }
 sent.push(JSON.parse(options.body));
 return {ok:true,json:async()=>({message:'connecting'})};
});
await tick();
assert.equal(scans,0);
assert.equal(s.node('save').disabled,true);
assert.match(s.node('info').textContent,/Giữ BOOT 5 giây/);
current.ap=true;
await s.context.poll();
await tick();
assert.equal(scans,1);
assert.equal(s.node('networks').children.length,3);
assert.equal(s.node('networks').children[1].textContent,'<img src=x onerror=alert(1)> (-42 dBm) · Sóng mạnh');
assert.equal(s.node('save').disabled,false);
assert.doesNotMatch(s.html,/<input id=(ip|mode|gateway|mask|dns)/);
s.node('ssid').value='Home';
s.node('password').value='example123';
await s.node('form').onsubmit({preventDefault(){}});
assert.deepEqual(sent[0],{ssid:'Home',password:'example123',mode:'dhcp'});
current={...current,connected:true,pending:false,ip:'192.168.10.44'};
await s.context.poll();
assert.equal(s.node('result').children[0].href,'http://192.168.10.44/');
assert.equal(s.node('result').children[1].href,'http://192.168.10.44/network');
assert.equal(s.node('password').value,'');
await s.node('form').onsubmit({preventDefault(){}});
current.error=3;
await s.context.poll();
assert.match(s.node('status').textContent,/Chưa kết nối/);
assert.equal(s.node('save').disabled,false);

const networkCalls=[];
const network=page('src/network_settings.c',async(url,options)=>{
 assert.equal(url,'/api/network');
 if(options?.method==='POST'){
   networkCalls.push(JSON.parse(options.body));
   return {ok:true,json:async()=>({message:'saved',url:'http://192.168.10.44/'})};
 }
 return {ok:true,json:async()=>({mode:'dhcp',current:'192.168.10.44'})};
});
await tick();
assert.match(network.html,/Đổi Wi-Fi/);
assert.doesNotMatch(network.html,/<input/);
assert.match(network.node('current').textContent,/192\.168\.10\.44/);
network.node('mode').value='static';
network.node('mode').onchange();
assert.match(network.node('preview').textContent,/192\.168\.10\.44/);
await network.node('form').onsubmit({preventDefault(){}});
assert.deepEqual(networkCalls,[{mode:'static'}]);
assert.equal(network.node('status').children[0].href,'http://192.168.10.44/');

const offline=page('src/network_settings.c',async()=>({ok:true,json:async()=>({mode:'dhcp',current:''})}));
await tick();
offline.node('mode').value='static';
offline.node('mode').onchange();
assert.equal(offline.node('save').disabled,true);

const calls=[];const u=page('src/cloud_update.c',async(url,options)=>{
 if(options?.method==='POST'){calls.push(JSON.parse(options.body));return {ok:false,json:async()=>({message:'BOOT 5 seconds required'})};}
 return {json:async()=>({current:'0.8.0',profile:'hp',latest:'0.9.0',available:true,busy:false,state:'checked',message:'new',progress:0})};
});
await tick();assert.equal(u.node('install').disabled,false);await u.node('install').onclick();await tick();assert.deepEqual(calls,[{action:'install'}]);assert.equal(u.alerts[0],'BOOT 5 seconds required');assert.match(s.html,/vietphuong esp/);assert.match(u.html,/leviethoaiphuong/);
console.log('Device pages: Wi-Fi discovery, credentials over DHCP, live-IP selection without credential fields, and OTA confirmation passed');
