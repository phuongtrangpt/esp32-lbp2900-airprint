import {hasAccess,redeemCode} from './access-session.js';
const host=document.querySelector('#installer'),dialog=document.querySelector('#access-dialog'),form=document.querySelector('#access-form'),input=document.querySelector('#access-code'),message=document.querySelector('#access-message'),submit=document.querySelector('#access-submit');
host.addEventListener('click',event=>{
 if(hasAccess())return;
 event.preventDefault();event.stopImmediatePropagation();input.value='';message.textContent='';dialog.showModal();input.focus();
},true);
document.querySelector('#access-close').addEventListener('click',()=>{input.value='';dialog.close();});
dialog.addEventListener('close',()=>{input.value='';});
form.addEventListener('submit',async event=>{
 event.preventDefault();submit.disabled=true;message.textContent='Đang xác thực…';
 try{await redeemCode(input.value);input.value='';dialog.close();document.querySelector('#access-state').textContent='Đã mở quyền nạp cho lần mở trang này. Bấm kết nối/nạp để tiếp tục.';}
 catch(error){message.textContent=error.message;}
 finally{submit.disabled=false;}
});
