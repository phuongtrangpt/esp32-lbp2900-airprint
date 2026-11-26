'use strict';
const $=id=>document.getElementById(id);
const invoke=window.__TAURI__.core.invoke;
let busy=false,pending=false,cancelPending=false,initialized=false,lastSequence=0,bootId='',lastError='';
const labels={'waiting-usb':'Đang chờ máy in nhận dữ liệu','device-status':'Trạng thái máy in','waiting-device':'Đang chờ máy in','service-started':'Dịch vụ Windows đã khởi động','service-stopped':'Dịch vụ Windows đã dừng','media-normalized':'Đã nhận dạng khổ giấy',ready:'Dịch vụ nhận lệnh đang chạy',spooled:'Đã nhận đủ tài liệu',rendering:'PC đang xử lý và nén',rendered:'PC đã xử lý xong',sending:'Đang gửi sang ESP','usb-delivered':'Đã chuyển qua USB — hãy kiểm tra giấy',failed:'Lệnh in gặp lỗi',canceled:'Đã hủy',stopped:'Bộ xử lý in đã dừng'};
function log(message,time){$('log').textContent=($('log').textContent+'\n'+new Date(time||Date.now()).toLocaleTimeString()+' · '+message).split('\n').slice(-60).join('\n');}
function event(e){
 const title=labels[e.phase]||e.phase;
 // Capability probes and device updates belong in history, not job progress.
 if(['waiting-usb','spooled','rendering','rendered','sending','waiting-device','usb-delivered','failed','canceled'].includes(e.phase))$('status').textContent=title+(e.message?' · '+e.message:'');
 if(e.phase==='sending')$('progress').value=e.done/e.total*100;else if(e.phase==='rendering')$('progress').removeAttribute('value');else if(e.phase==='usb-delivered')$('progress').value=100;
 if(e.phase!=='sending')log(title+(e.message?' · '+e.message:'')+(e.render_ms!==undefined?' · '+e.pages+' trang, PC '+(e.render_ms/1000).toFixed(2)+' s, '+Math.ceil(e.raster_bytes/1024)+' → '+Math.ceil(e.bytes/1024)+' KiB':'')+(e.send_ms!==undefined?' · gửi '+(e.send_ms/1000).toFixed(2)+' s':''),e.time_ms);
}
async function refresh(){
 try{
  const s=await invoke('get_snapshot');lastError='';
  if(!initialized){$('ip').value=s.settings?.esp_ip||localStorage.getItem('esp-ip')||'';initialized=true;}
  if(s.boot_id!==bootId){bootId=s.boot_id;lastSequence=0;}
  for(const e of s.events||[])if(e.sequence>lastSequence){event(e);lastSequence=e.sequence;}
  const device=s.device||{};const names={ready:'Sẵn sàng',busy:'Máy in đang bận',offline:'Offline — không kết nối',checking:'Đang kiểm tra kết nối',error:'Máy in cần xử lý lỗi'};$('device').textContent=(names[device.state]||'Đang kiểm tra máy in')+(device.message?' · '+device.message:'');
  busy=!!s.busy;$('ip').disabled=busy||pending;$('start').disabled=busy||pending;
  const running=s.service_state==='Running'&&s.ready;
  $('cancel').disabled=!running||!busy||cancelPending;
  $('progress').hidden=!running||!busy;
  if(!busy){$('progress').value=0;if(running&&s.settings?.esp_ip)$('status').textContent='Không có lệnh đang xử lý · '+(device.state==='ready'?'Sẵn sàng nhận lệnh mới':device.message||'Đang kiểm tra máy in');}
  $('target').textContent='Windows Service: '+s.service_state+(s.esp?' · ESP '+s.esp+':9101':' · Chưa lưu IP ESP');
  if(s.service_state==='NotInstalled')$('status').textContent='Chưa có Windows Service. Hãy cài bản 0.2.0 bằng quyền quản trị.';
  else if(s.service_state==='Stopped')$('status').textContent='Dịch vụ đã dừng để thay máy. Bấm Lưu IP / bật dịch vụ khi sẵn sàng.';
  else if(!s.settings?.esp_ip)$('status').textContent='Nhập IP ESP rồi bấm Lưu IP / bật dịch vụ một lần.';
 }catch(e){if(String(e)!==lastError){lastError=String(e);log(lastError);}}
}
async function command(id,name,args){$(id).disabled=true;try{const result=await invoke(name,args);if(result)log(typeof result==='string'?result:JSON.stringify(result));return true;}catch(e){log(String(e));$('status').textContent=String(e);return false;}finally{$(id).disabled=id==='cancel'||id==='start'&&(busy||pending);}}
$('check').onclick=()=>command('check','check_esp',{ip:$('ip').value.trim()});
$('start').onclick=async()=>{if(busy)return;pending=true;$('ip').disabled=true;const ip=$('ip').value.trim();if(await command('start','start_worker',{ip})){localStorage.setItem('esp-ip',ip);log('Đã lưu IP cho Windows Service. Dịch vụ sẽ tự chạy mỗi khi bật máy.');}pending=false;await refresh();};
$('stop').onclick=()=>command('stop','stop_worker').then(refresh);
$('cancel').onclick=async()=>{if(!busy||cancelPending)return;cancelPending=true;try{await command('cancel','cancel_job');}finally{cancelPending=false;await refresh();}};
$('install').onclick=()=>command('install','install_queue');
$('hide').onclick=()=>invoke('hide_window');
$('quit').onclick=()=>invoke('quit_manager');
refresh();setInterval(()=>{if(!document.hidden)refresh();},1500);
