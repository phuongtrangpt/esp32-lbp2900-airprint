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
let current={ap_name:'vietphuong esp-A123',error:0,connected:false,pending:false,ip:''};const sent=[];
const s=page('src/setup_portal.c',async(url,options)=>{
 if(url.endsWith('/status'))return {json:async()=>current};
 if(url.endsWith('/scan'))return {ok:true,json:async()=>({networks:[{ssid:'<img src=x onerror=alert(1)>',rssi:-42}]})};
 sent.push(JSON.parse(options.body));return {ok:true,json:async()=>({message:'connecting'})};
});
await tick();assert.equal(s.node('ip').disabled,true);s.node('mode').value='static';s.node('mode').onchange();assert.equal(s.node('ip').disabled,false);assert.equal(s.node('ip').required,true);
await s.node('scan').onclick();assert.equal(s.node('networks').children[0].textContent,'<img src=x onerror=alert(1)> (-42 dBm)');
s.node('ssid').value='Home';s.node('password').value='example123';s.node('ip').value='192.168.10.44';
await s.node('form').onsubmit({preventDefault(){}});assert.equal(sent[0].ssid,'Home');assert.equal(sent[0].ip,'192.168.10.44');
current={...current,connected:true,pending:false,ip:'192.168.10.44'};await s.context.poll();assert.equal(s.node('result').children[0].href,'http://192.168.10.44/');assert.equal(s.node('password').value,'');
await s.node('form').onsubmit({preventDefault(){}});current.error=3;await s.context.poll();assert.match(s.node('status').textContent,/Chưa kết nối/);assert.equal(s.node('save').disabled,false);
const calls=[];const u=page('src/cloud_update.c',async(url,options)=>{
 if(options?.method==='POST'){calls.push(JSON.parse(options.body));return {ok:false,json:async()=>({message:'BOOT 5 seconds required'})};}
 return {json:async()=>({current:'0.8.0',profile:'hp',latest:'0.9.0',available:true,busy:false,state:'checked',message:'new',progress:0})};
});
await tick();assert.equal(u.node('install').disabled,false);await u.node('install').onclick();await tick();assert.deepEqual(calls,[{action:'install'}]);assert.equal(u.alerts[0],'BOOT 5 seconds required');assert.match(s.html,/vietphuong esp/);assert.match(u.html,/leviethoaiphuong/);
console.log('Device pages: branded setup, DHCP/static toggle, safe SSID text, queued credentials, success/error recovery, OTA status/physical-confirmation response passed');
