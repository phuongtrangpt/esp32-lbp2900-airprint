export const profiles={
 hp:{label:'HP LaserJet P1006',models:['HP LaserJet P1006'],note:'Bản riêng cho P1006. Đã có xác nhận in thực tế trên các bản trước; có sẵn file khởi động HP.'},
 hp_p1008:{label:'HP LaserJet P1008',models:['HP LaserJet P1008'],note:'Bản thử nghiệm riêng cho P1008 trên ESP32-S3 N16R8. Cần thử trên máy in thật. Có sẵn file khởi động HP.'},
 canon:{label:'Canon LBP2900',models:['Canon LBP2900'],note:'Bản chính thức 0.9.9, CAPT riêng cho LBP2900. Không dùng chung cho Canon LBP6030 hoặc các máy Canon laser khác.'},
 lbp6030:{label:'Canon LBP6030 · Thử nghiệm USB',models:['Canon LBP6030','Canon LBP6030B','Canon LBP6030w'],note:'Nạp qua USB. Cần xác nhận in trên máy thật cho từng biến thể.'},
 pixma:{label:'Canon PIXMA USB',models:['Canon MG2500 series','Canon MG2400 series','Canon P200 series','Canon MG2900 series','Canon MG3000 series','Canon E470 series','Canon E460 series','Canon iP2800 series','Canon E3100 series','Canon TS3100 series','Canon MP230 series','Canon iP2700 series','Canon MP250 series','Canon MP270 series','Canon MP280 series','Canon MP490 series','Canon MP493 series','Canon MP495 series','Canon MX340 series','Canon MX350 series','Canon MX360 series','Canon MX410 series','Canon MX420 series'],note:'Driver có trong mã nguồn, bản ghép mới chưa được chúng tôi kiểm chứng trên máy thật. Ưu tiên các dòng USB MG2400/MG2500, MP230, iP2700/iP2800, MP250/270/280. Tên series phải khớp tên USB; không tự suy ra mọi biến thể hậu tố. Một số dòng trong danh sách vốn có Wi-Fi, không cần mua ESP nếu Wi-Fi sẵn có đáp ứng nhu cầu.'},
 standard:{label:'Máy USB PCL5 / PostScript',models:['HP LaserJet 1320','HP LaserJet P2015','HP LaserJet P2055'],note:'Các model này có PCL5/PostScript theo tài liệu hãng, là ứng viên cần thử trên máy thật. ESP chỉ nhận khi máy khai báo PCL/PCL5/PCL5e/PCL5c hoặc PostScript qua USB. Chỉ in đen trắng, một mặt A4/Letter. Không hỗ trợ máy chỉ có PCL6/PCL XL, UFR II, GDI hoặc host-based khác.'}
};
export const excluded=['Canon LBP3300','Canon LBP6230dn','Canon LBP121dn','Canon LBP122dw','Canon LBP151dw'];
export const underResearch=['Brother HL-1201','Canon LBP3010','Canon LBP3018','Canon LBP3050','HP LaserJet 1018','HP LaserJet 1020'];
const norm=s=>s.toLowerCase().replace(/series/g,'').replace(/[^a-z0-9]/g,'');
export function lookupModel(value){
 const q=norm(value);if(!q)return null;
 for(const model of excluded){const key=norm(model);if(q===key||q===key.replace(/^canon/,''))return {unsupported:true,model,reason:model==='Canon LBP3300'?'capt':'ufr'};}
 for(const model of underResearch){const key=norm(model);if(q===key||q===key.replace(/^(brother|canon|hplaserjet)/,''))return {research:true,model};}
 for(const [profile,info] of Object.entries(profiles))for(const model of info.models){const key=norm(model);const short=key.replace(/^(canon|hp)(laserjet)?/,'');if(q===key||q===short)return {profile,model,info};}
 return {unknown:true};
}
