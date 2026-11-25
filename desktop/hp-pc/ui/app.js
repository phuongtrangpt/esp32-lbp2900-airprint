'use strict';
const $=id=>document.getElementById(id);
const invoke=window.__TAURI__.core.invoke;
const labels={ready:'Dịch vụ đang chạy',spooled:'Đã nhận đủ tài liệu',rendering:'PC đang xử lý và nén',rendered:'PC đã xử lý xong',sending:'Đang gửi sang ESP', 'usb-delivered':'Đã chuyển qua USB — hãy kiểm tra giấy',failed:'Lệnh in gặp lỗi',canceled:'Đã hủy',stopped:'Dịch vụ đã dừng','ipp-error':'Yêu cầu in bị từ chối','upload-error':'Tài liệu truyền chưa đầy đủ'};
const saved=localStorage.getItem('esp-ip');if(saved)$('ip').value=saved;
function log(message){$('log').textContent=($('log').textContent+'\n'+new Date().toLocaleTimeString()+' · '+message).split('\n').slice(-40).join('\n');}
async function command(id,name,args){$(id).disabled=true;try{const result=await invoke(name,args);if(result)log(result);return true;}catch(e){$('status').textContent=String(e);log(String(e));return false;}finally{$(id).disabled=false;}}
$('check').onclick=()=>command('check','check_esp',{ip:$('ip').value.trim()});
$('start').onclick=async()=>{const ip=$('ip').value.trim();if(await command('start','start_worker',{ip}))localStorage.setItem('esp-ip',ip);};
$('stop').onclick=()=>command('stop','stop_worker');
$('cancel').onclick=()=>command('cancel','cancel_job');
$('install').onclick=()=>command('install','install_queue');
window.__TAURI__.event.listen('worker-event',({payload:e})=>{const title=labels[e.phase]||e.phase;$('status').textContent=title+(e.message?' · '+e.message:'');if(e.phase==='sending')$('progress').value=e.done/e.total*100;else if(e.phase==='rendering')$('progress').removeAttribute('value');else if(e.phase==='usb-delivered')$('progress').value=100;else if(e.phase==='ready'||e.phase==='failed'||e.phase==='canceled')$('progress').value=0;if(e.phase!=='sending')log(title+(e.message?' · '+e.message:'')+(e.render_ms!==undefined?' · '+e.pages+' trang, PC '+(e.render_ms/1000).toFixed(2)+' s, '+Math.ceil(e.raster_bytes/1024)+' → '+Math.ceil(e.bytes/1024)+' KiB':'')+(e.send_ms!==undefined?' · gửi '+(e.send_ms/1000).toFixed(2)+' s':''));});
