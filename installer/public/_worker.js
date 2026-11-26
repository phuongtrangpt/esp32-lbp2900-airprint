// Codes are consumed atomically in D1. Never use an isolate-local cache for redemption.
const trusted = new Set(['https://esp.vietphuong.io.vn','https://key.vietphuong.io.vn','https://vietphuong-esp.pages.dev']);
const allowed = origin => trusted.has(origin) || /^https:\/\/[a-z0-9-]+\.vietphuong-esp\.pages\.dev$/.test(origin);
const hex = bytes => Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
const random = () => hex(crypto.getRandomValues(new Uint8Array(16)));
const digest = async value => hex(new Uint8Array(await crypto.subtle.digest('SHA-256',new TextEncoder().encode(value))));
function json(data,status=200){return Response.json(data,{status,headers:{'Cache-Control':'no-store','Referrer-Policy':'no-referrer','X-Content-Type-Options':'nosniff'}});}
export default {
 async fetch(request,env){
  const url=new URL(request.url);
  // This hostname can later be attached to the same Pages project.
  if(url.hostname==='key.vietphuong.io.vn'&&url.pathname==='/')return Response.redirect(url.origin+'/lay-ma/',302);
  if(!url.pathname.startsWith('/api/access/'))return env.ASSETS.fetch(request);
  if(request.method!=='POST')return json({error:'Chỉ nhận yêu cầu POST.'},405);
  const origin=request.headers.get('Origin');
  if(!origin||!allowed(origin)||origin!==url.origin)return json({error:'Nguồn yêu cầu không hợp lệ.'},403);
  if(!request.headers.get('Content-Type')?.startsWith('application/json'))return json({error:'Yêu cầu không hợp lệ.'},415);
  if(!env.KEY_DB)return json({error:'Hệ thống cấp mã chưa sẵn sàng. Vui lòng thử lại sau.'},503);
  try{
   const text=await request.text();if(text.length>2048)return json({error:'Yêu cầu quá dài.'},413);
   const data=JSON.parse(text),now=Math.floor(Date.now()/1000);
   if(!data||typeof data!=='object')return json({error:'Yêu cầu không hợp lệ.'},400);
   const action=url.pathname.slice('/api/access/'.length);
   if(!['new','redeem','check'].includes(action))return json({error:'Không tìm thấy.'},404);
   if(action!=='check'){
    const ip=request.headers.get('CF-Connecting-IP')||'local';
    const bucket=Math.floor(now/60),hash=await digest(`${action}:${ip}:${bucket}`);
    const row=await env.KEY_DB.prepare('INSERT INTO access_limits(hash,count,expires) VALUES(?,1,?) ON CONFLICT(hash) DO UPDATE SET count=count+1 RETURNING count').bind(hash,now+120).first();
    if(row.count>(action==='new'?12:30))return json({error:'Bạn thao tác quá nhanh. Vui lòng chờ một phút rồi thử lại.'},429);
    await env.KEY_DB.batch([
     env.KEY_DB.prepare('DELETE FROM access_codes WHERE expires<=?').bind(now),
     env.KEY_DB.prepare('DELETE FROM access_sessions WHERE expires<=?').bind(now),
     env.KEY_DB.prepare('DELETE FROM access_limits WHERE expires<=?').bind(now)
    ]);
   }
   if(action==='new'){
    const code=random();await env.KEY_DB.prepare('INSERT INTO access_codes(hash,expires) VALUES(?,?)').bind(await digest(code),now+300).run();
    return json({code,expiresAt:(now+300)*1000});
   }
   if(action==='redeem'){
    const code=typeof data.code==='string'?data.code.trim().toLowerCase():'';
    if(!/^[a-f0-9]{32}$/.test(code))return json({error:'Mã không hợp lệ. Hãy sao chép đầy đủ mã vừa tạo.'},400);
    const token=random(),tokenHash=await digest(token),codeHash=await digest(code);
    // One statement wins under concurrent redemption; INSERT uses the delete result.
    // D1 batch is transactional: failed session creation rolls back code consumption.
    const results=await env.KEY_DB.batch([
     env.KEY_DB.prepare('INSERT INTO access_sessions(hash,expires) SELECT ?,? WHERE EXISTS(SELECT 1 FROM access_codes WHERE hash=? AND expires>?)').bind(tokenHash,now+86400,codeHash,now),
     env.KEY_DB.prepare('DELETE FROM access_codes WHERE hash=? AND expires>? RETURNING hash').bind(codeHash,now)
    ]);
    if(!results[1].results.length)return json({error:'Mã đã dùng hoặc hết hạn. Hãy lấy mã mới.'},409);
    return json({token});
   }
   const token=typeof data.token==='string'?data.token:'';
   if(!/^[a-f0-9]{32}$/.test(token))return json({error:'Hãy nhập mã để tiếp tục nạp.'},401);
   const row=await env.KEY_DB.prepare('SELECT hash FROM access_sessions WHERE hash=? AND expires>?').bind(await digest(token),now).first();
   return row?json({ok:true}):json({error:'Phiên nạp đã hết hạn. Hãy lấy mã mới.'},401);
  }catch{return json({error:'Chưa xử lý được yêu cầu. Vui lòng thử lại.'},503);}
 }
};
