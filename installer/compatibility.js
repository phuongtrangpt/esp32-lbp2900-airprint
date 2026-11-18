import {profiles,excluded,lookupModel} from './compatibility-data.js';
const select=document.querySelector('#printer-model');
const note=document.querySelector('#compatibility-note');
function update(){note.textContent=profiles[select.value]?.note||'';}
select.addEventListener('change',update);queueMicrotask(update);
const list=document.querySelector('#known-models');
for(const model of [...Object.values(profiles).flatMap(p=>p.models),...excluded]){const option=document.createElement('option');option.value=model;list.append(option);}
const input=document.querySelector('#model-search'),result=document.querySelector('#model-result'),choose=document.querySelector('#choose-profile');
let found;
input.addEventListener('input',()=>{
 found=lookupModel(input.value);choose.hidden=true;
 if(!found){result.textContent='Nhập model trên vỏ máy, ví dụ: LBP2900, P1006, MP280, P2015.';return;}
 if(found.unsupported){result.textContent=`${found.model}: chưa hỗ trợ trong các bản hiện tại. Không chọn bản LBP2900 thay thế. Máy UFR II/UFR II LT hoặc chỉ PCL6 cần giải pháp driver khác.`;return;}
 if(found.unknown){result.textContent='Chưa có tên này trong danh sách. Không tự chọn bản gần giống. Kiểm tra đúng model hoặc liên hệ hỗ trợ trước khi mua mạch.';return;}
 result.textContent=`${found.model} → ${found.info.label}. ${found.info.note}`;
 choose.textContent=`Chọn bản ${found.info.label}`;choose.hidden=false;
});
choose.addEventListener('click',()=>{if(!found?.profile)return;select.value=found.profile;select.dispatchEvent(new Event('change'));select.focus();select.scrollIntoView({behavior:'smooth',block:'center'});});
const groups=document.querySelector('#support-groups');
for(const info of Object.values(profiles)){const row=document.createElement('details'),title=document.createElement('summary'),names=document.createElement('p'),note=document.createElement('p');title.textContent=`${info.label} · ${info.models.length} tên dòng/model`;names.textContent=info.models.join(' · ');note.textContent=info.note;row.append(title,names,note);groups.append(row);}
