import './style.css';
import 'esp-web-tools/dist/install-button.js';
const state=document.querySelector('#load-state');
const installer=document.querySelector('#installer');
const diagnostics=document.querySelector('#flash-diagnostics');
window.addEventListener('installer-state',({detail})=>{
  diagnostics.hidden=false;
  diagnostics.textContent=detail.message;
  diagnostics.classList.toggle('warn',detail.state==='error');
});
const model=document.querySelector("#printer-model");
model.value=new URLSearchParams(location.search).get("model")==="canon"?"canon":"hp";
model.addEventListener("change",ready);
async function ready() {
  installer.style.display="none";
  const selected=model.value;
  const manifestURL=new URL(`./firmware/${selected}/web-manifest.json`,document.baseURI);
  if (!window.isSecureContext || !('serial' in navigator)) {
    state.textContent='Mở bằng Chrome hoặc Edge trên máy tính để nạp qua USB.';
    state.classList.add('warn');
    document.querySelector('#version').textContent='Web Serial chưa khả dụng';
    return;
  }
  try {
    const response=await fetch(manifestURL,{cache:'no-store'});
    if(!response.ok) throw new Error('Manifest chưa sẵn sàng');
    const manifest=await response.json();
    if(selected!==model.value)return;
    if(!manifest.builds?.some(b=>b.chipFamily==='ESP32-S3')) throw new Error('Gói firmware không đúng chip');
    // Resolve from the document: Vite moves this script into assets/ at build time.
    installer.manifest=manifestURL.href;
    document.querySelector('#version').textContent=manifest.version;
    document.querySelector('#selected-model').textContent=manifest.name;
    state.textContent='Gói firmware đã sẵn sàng · Chọn đúng board N16R8 trước khi nạp';
    state.classList.add('ok'); installer.style.display='block';
  } catch {
    if(selected!==model.value)return;
    state.textContent='Gói firmware chưa tải được. Hãy tải lại trang hoặc kiểm tra kết nối Internet.';
    state.classList.add('warn');
    document.querySelector('#version').textContent='Chưa tải được firmware';
  }
}
ready();

const refresh=document.querySelector('#refresh-release');
if(refresh)refresh.addEventListener('click',async()=>{refresh.disabled=true;document.querySelector('#refresh-status').textContent='Đang kiểm tra phiên bản…';await ready();document.querySelector('#refresh-status').textContent='Đã kiểm tra. Nạp lại qua USB hoặc dùng /update trên ESP để cập nhật qua Wi-Fi.';refresh.disabled=false;});
