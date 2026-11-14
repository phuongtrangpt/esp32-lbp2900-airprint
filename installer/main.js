import './style.css';
import 'esp-web-tools/dist/web/install-button.js';
const state=document.querySelector('#load-state');
const installer=document.querySelector('#installer');
async function ready() {
  if (!window.isSecureContext || !('serial' in navigator)) {
    state.textContent='Mở bằng Chrome hoặc Edge trên máy tính để nạp qua USB.';
    state.classList.add('warn');
    document.querySelector('#version').textContent='Web Serial chưa khả dụng';
    return;
  }
  try {
    const response=await fetch(new URL('./firmware/web-manifest.json',document.baseURI),{cache:'no-store'});
    if(!response.ok) throw new Error('Manifest chưa sẵn sàng');
    const manifest=await response.json();
    if(!manifest.builds?.some(b=>b.chipFamily==='ESP32-S3')) throw new Error('Gói firmware không đúng chip');
    // Resolve from the document: Vite moves this script into assets/ at build time.
    installer.manifest=new URL('./firmware/web-manifest.json',document.baseURI).href;
    document.querySelector('#version').textContent=manifest.version;
    state.textContent='Gói firmware đã sẵn sàng · Chọn đúng board N16R8 trước khi nạp';
    state.classList.add('ok'); installer.style.display='block';
  } catch {
    state.textContent='Gói firmware chưa tải được. Hãy tải lại trang hoặc kiểm tra kết nối Internet.';
    state.classList.add('warn');
    document.querySelector('#version').textContent='Chưa tải được firmware';
  }
}
ready();
