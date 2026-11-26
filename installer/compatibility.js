import {selectProfile} from './workspace.js';
import {profiles,excluded,underResearch,lookupModel} from './compatibility-data.js';
const select=document.querySelector('#printer-model');
const note=document.querySelector('#compatibility-note');
const feedback=document.querySelectorAll('[data-feedback-profile]');
function update(){
 note.textContent=profiles[select.value]?.note||'';
 for(const panel of feedback)panel.hidden=panel.dataset.feedbackProfile!==select.value;
}
select.addEventListener('change',update);queueMicrotask(update);
const list=document.querySelector('#known-models');
for(const model of [...Object.values(profiles).flatMap(p=>p.models),...excluded,...underResearch]){const option=document.createElement('option');option.value=model;list.append(option);}
const input=document.querySelector('#model-search'),result=document.querySelector('#model-result'),choose=document.querySelector('#choose-profile');
let found;
input.addEventListener('input',()=>{
 found=lookupModel(input.value);choose.hidden=true;
 if(!found){result.textContent='Nhập model trên vỏ máy, ví dụ: LBP2900, P1006, MP280, P2015.';return;}
 if(found.unsupported){result.textContent=`${found.model}: chưa hỗ trợ trong các bản hiện tại. Không chọn bản LBP2900 thay thế. ${found.reason==='capt'?'LBP3300 dùng CAPT nhưng chưa có driver riêng cho model này.':'Máy UFR II/UFR II LT hoặc chỉ PCL6 cần giải pháp driver khác.'}`;return;}
 if(found.research){result.textContent=`${found.model}: đang nghiên cứu driver, chưa có firmware để tải.`;return;}
 if(found.unknown){result.textContent='Chưa có tên này trong danh sách. Không tự chọn bản gần giống. Kiểm tra đúng model hoặc liên hệ hỗ trợ trước khi mua mạch.';return;}
 result.textContent=`${found.model} → ${found.info.label}. ${found.info.note}`;
 choose.textContent=`Chọn bản ${found.info.label}`;choose.hidden=false;
});
choose.addEventListener('click',()=>{if(found?.profile)selectProfile(found.profile);});
const groups=document.querySelector('#support-groups');
const brands=[['HP',['hp','hp_p1008','standard']],['Canon',['canon','lbp6000','lbp6200','lbp3010','lbp6030','pixma']]];
for(const [brand,keys] of brands){
 const branch=document.createElement('details'),title=document.createElement('summary'),tree=document.createElement('ul');
 title.textContent=brand;tree.className='model-tree';branch.className='brand-branch';branch.open=true;
 for(const key of keys){
  const info=profiles[key],item=document.createElement('li'),row=document.createElement('details'),name=document.createElement('summary'),names=document.createElement('p'),note=document.createElement('p'),actions=document.createElement('div');
  name.textContent=key==='hp'?'LaserJet P1006':info.label.replace(/^Canon |^HP /,'');names.textContent=info.models.join(' · ');note.textContent=info.note;actions.className='tree-actions';
  const add=(profile,text)=>{const button=document.createElement('button');button.type='button';button.className='outline-button';button.textContent=text;button.addEventListener('click',()=>selectProfile(profile));actions.append(button);};
  add(key,key==='hp'?'In trực tiếp · Chọn firmware':'Chọn firmware này');
  if(key==='hp'){const pc=document.createElement('p');pc.textContent='Print Connect: Windows xử lý tài liệu; cần cài phần mềm hỗ trợ riêng.';row.append(name,names,note,pc);add('hp_pc','Print Connect · Chọn firmware');}else row.append(name,names,note);
  row.append(actions);item.append(row);tree.append(item);
 }
 branch.append(title,tree);groups.append(branch);
}
