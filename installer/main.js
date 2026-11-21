import './compatibility.js';
import './progress.js';
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
const requested=new URLSearchParams(location.search).get("model");
model.value=["hp","hp_p1008","canon","pixma","standard"].includes(requested)?requested:"hp";
model.addEventListener("change",ready);
async function ready() {
  installer.style.display="none";
  const selected=model.value;
  const manifestURL=new URL(`./firmware/${selected}/web-manifest.json`,document.baseURI);
  const supported=window.isSecureContext && ('serial' in navigator);
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
    state.textContent=supported?'Gói firmware đã sẵn sàng · Chọn đúng board N16R8 trước khi nạp':'Điện thoại: cấu hình Wi-Fi và cập nhật tại trang ESP. Nạp USB lần đầu dùng Chrome/Edge trên máy tính.';
    state.classList.toggle('ok',supported);state.classList.toggle('warn',!supported);
    installer.style.display=supported?'block':'none';
    return manifest;
  } catch {
    if(selected!==model.value)return;
    state.textContent='Gói firmware chưa tải được. Hãy tải lại trang hoặc kiểm tra kết nối Internet.';
    state.classList.add('warn');
    document.querySelector('#version').textContent='Chưa tải được firmware';
    return false;
  }
}
ready();

const refresh=document.querySelector('#refresh-release');
if(refresh)refresh.addEventListener('click',async()=>{refresh.disabled=true;document.querySelector('#refresh-status').textContent='Đang kiểm tra phiên bản…';const manifest=await ready();document.querySelector('#refresh-status').textContent=manifest?`Phiên bản trên web: ${manifest.version}. Dùng /update trên ESP để cập nhật qua Wi-Fi, hoặc nạp lại bằng USB.`:'Chưa kiểm tra được phiên bản. Hãy kiểm tra kết nối rồi thử lại.';refresh.disabled=false;});
