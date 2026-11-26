const panels=[...document.querySelectorAll('[data-panel]')];
const links=[...document.querySelectorAll('[data-view]')];
export function viewForHash(hash){return ({'#may-in':'may-in','#phan-mem':'phan-mem','#print-connect':'phan-mem','#huong-dan':'huong-dan','#phone-setup':'huong-dan'})[hash]||'nap';}
function render(){const view=viewForHash(location.hash);for(const p of panels)p.hidden=p.dataset.panel!==view;for(const a of links){if(a.dataset.view===view)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current');}}
window.addEventListener('hashchange',render);render();
export function selectProfile(profile){const select=document.querySelector('#printer-model');select.value=profile;select.dispatchEvent(new Event('change'));location.hash='nap';render();select.focus({preventScroll:true});}
for(const b of document.querySelectorAll('[data-select-profile]'))b.addEventListener('click',()=>selectProfile(b.dataset.selectProfile));
