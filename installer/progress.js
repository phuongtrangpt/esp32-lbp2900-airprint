import {createProgress,updateProgress} from './progress-state.js';
let current=createProgress();
const views=new Set();
window.addEventListener('installer-state',({detail})=>{
  current=updateProgress(current,detail,new Date().toLocaleTimeString('vi-VN',{hour12:false}));
  for(const view of views)view.render();
});
class FlashProgress extends HTMLElement{
  constructor(){super();this.attachShadow({mode:'open'});}
  connectedCallback(){views.add(this);this.render();}
  disconnectedCallback(){views.delete(this);}
  render(){
    this.hidden=!current.visible;
    if(!current.visible)return;
    if(!this.shadowRoot.childElementCount){
      this.shadowRoot.innerHTML=`<style>:host{display:block;margin:18px 0 4px;color:#c4d1e4;font:11px/1.7 ui-monospace,SFMono-Regular,Consolas,monospace;min-width:0}:host([hidden]){display:none}.box{background:#080f1c;border:1px solid #344255;border-radius:12px;overflow:hidden}.top{padding:10px 13px;background:#152033;display:flex;align-items:center;gap:7px;font-size:10px;color:#a6b8cd}.dot{width:6px;height:6px;border-radius:50%;background:#c5f36b}.title{margin-left:5px;flex:1}.status{padding:13px 13px 5px;color:#d8e5f4;overflow-wrap:anywhere}.meter{height:5px;background:#26354a;margin:8px 13px;overflow:hidden;border-radius:5px}.fill{height:100%;background:#c5f36b;width:0;transition:width .15s}.info{display:flex;justify-content:space-between;padding:0 13px 9px;color:#98a9c0;font-size:10px}.lines{padding:10px 13px;max-height:180px;overflow:auto;border-top:1px solid #26354a;scrollbar-width:thin;overflow-wrap:anywhere}.line{margin-bottom:5px;white-space:pre-wrap}.time{color:#73869e;margin-right:8px}.message{color:#bad5ae}.error .message{color:#ffaf9c}.note{padding:8px 13px;border-top:1px solid #26354a;font:10px/1.6 system-ui;color:#91a3bd}.error-fill{background:#ff9987}@media(prefers-reduced-motion:reduce){.fill{transition:none}}</style><section class="box" aria-label="Tiến trình nạp firmware"><div class="top"><span class="dot"></span><span class="title">VIETPHUONG ESP / FLASH CONSOLE</span><span class="phase"></span></div><div class="status" role="status" aria-live="polite"></div><div class="meter" role="progressbar" aria-label="Phần trăm ghi firmware" aria-valuemin="0" aria-valuemax="100"><div class="fill"></div></div><div class="info"><span class="percentage"></span><span class="bytes"></span></div><div class="lines" role="log" aria-label="Nhật ký nạp"></div><div class="note">Giữ nguyên cáp USB đến khi thông báo nạp hoàn tất. Phần trăm thể hiện dữ liệu ghi vào flash.</div></section>`;
    }
    const root=this.shadowRoot;
    root.querySelector('.status').textContent=current.status;
    root.querySelector('.phase').textContent=current.state==='error'?'LỖI':current.state==='finished'?'HOÀN TẤT':'ĐANG CHẠY';
    root.querySelector('.percentage').textContent=current.percent===null?'Đang chuẩn bị…':`${current.percent}% đã ghi`;
    root.querySelector('.bytes').textContent=current.bytes;
    root.querySelector('.fill').style.width=`${current.percent??0}%`;
    root.querySelector('.fill').classList.toggle('error-fill',current.state==='error');
    const meter=root.querySelector('.meter');
    if(current.percent===null)meter.removeAttribute('aria-valuenow');else meter.setAttribute('aria-valuenow',String(current.percent));
    const lines=root.querySelector('.lines');const follow=lines.scrollTop+lines.clientHeight>=lines.scrollHeight-30;
    lines.replaceChildren(...current.lines.map(line=>{const row=document.createElement('div');row.className=line.error?'line error':'line';const time=document.createElement('span');time.className='time';time.textContent=`[${line.time}]`;const message=document.createElement('span');message.className='message';message.textContent=line.message;row.append(time,message);return row;}));
    if(follow)lines.scrollTop=lines.scrollHeight;
  }
}
customElements.define('vp-flash-progress',FlashProgress);
