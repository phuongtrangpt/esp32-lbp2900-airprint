// Intentionally memory-only: no cookies, localStorage, sessionStorage or URL tokens.
let token='';
export async function redeemCode(code){
 const response=await fetch('/api/access/redeem',{method:'POST',cache:'no-store',headers:{'Content-Type':'application/json'},body:JSON.stringify({code})});
 const data=await response.json();if(!response.ok)throw Error(data.error||'Không xác thực được mã.');
 token=data.token;
}
export function hasAccess(){return !!token;}
export async function authorizeFlash(){
 if(!token)throw Error('Hãy nhập mã giới thiệu trước khi nạp.');
 const response=await fetch('/api/access/check',{method:'POST',cache:'no-store',headers:{'Content-Type':'application/json'},body:JSON.stringify({token})});
 if(!response.ok){if(response.status===401)token='';const data=await response.json();throw Error(data.error||'Chưa kiểm tra được quyền nạp. Hãy thử lại.');}
}
