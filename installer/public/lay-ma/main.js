const $=id=>document.getElementById(id);let expires=0;
$('create').onclick=async()=>{
 $('create').disabled=true;$('status').textContent='Đang tạo mã…';
 try{const r=await fetch('/api/access/new',{method:'POST',cache:'no-store',headers:{'Content-Type':'application/json'},body:'{}'});const data=await r.json();if(!r.ok)throw Error(data.error||'Chưa tạo được mã.');$('code').value=data.code;expires=data.expiresAt;$('result').hidden=false;$('status').textContent='Mã đã sẵn sàng. Sao chép rồi dán vào trang nạp.';tick();}
 catch(e){$('status').textContent=e.message;}finally{$('create').disabled=false;}
};
$('copy').onclick=async()=>{try{await navigator.clipboard.writeText($('code').value);$('status').textContent='Đã sao chép mã. Quay lại tab trang nạp để dán.';}catch{$('code').focus();$('code').select();$('status').textContent='Hãy nhấn Ctrl+C hoặc giữ ô mã để sao chép.';}};
function tick(){const left=Math.max(0,Math.ceil((expires-Date.now())/1000));$('copy').disabled=!left;$('expires').textContent=left?`Còn ${Math.floor(left/60)} phút ${left%60} giây để dùng mã.`:'Mã đã hết hạn. Hãy tạo mã mới.';if(!left)$('code').value='';}
setInterval(()=>{if(expires)tick();},1000);
