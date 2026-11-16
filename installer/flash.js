// ESP Web Tools flash adapter, Apache-2.0; based on esphome/esp-web-tools src/flash.ts.
// Keep the upstream Improv/Wi-Fi dialog; make serial recovery and errors observable.
import { Transport, ESPLoader } from 'esptool-js';
import { connectLoader, releaseTransport } from './serial.js';

export async function flash(onEvent, port, manifestPath, manifest, eraseFirst, dependencies={Transport,ESPLoader}) {
  let transport, loader, build, chipFamily, phase='failed_initialize';
  const emit=(state,message,details={})=>{
    const event={state,message,details,manifest,build,chipFamily};
    window.dispatchEvent(new CustomEvent('installer-state',{detail:event}));
    onEvent(event);
  };
  let outcome;
  try {
    emit('initializing','Đang kết nối ESP32-S3…',{done:false});
    ({transport,loader}=await connectLoader(port,dependencies,message=>
      emit('initializing',message,{done:false})));
    chipFamily=loader.chip.CHIP_NAME;
    // Wi-Fi provisioning uses UART0. Do not silently offer a UART build on CDC.
    const info=port.getInfo();
    const serialType=info.usbVendorId===0x303a && [0x1001,0x1002,0x1003,2,3].includes(info.usbProductId)?'cdc':'uart';
    build=manifest.builds.find(b=>b.chipFamily===chipFamily && (!b.serialType || b.serialType===serialType));
    if(!build) { phase='not_supported'; throw new Error(`${chipFamily} / ${serialType}: hãy chọn cổng UART/COM của ESP32-S3 N16R8. Cổng USB OTG dành cho máy in.`); }
    emit('initializing',`Đã nhận ${chipFamily} · ${serialType}`,{done:true});
    phase='failed_firmware_download';
    emit('preparing','Đang tải và kiểm tra firmware…',{done:false});
    const files=await Promise.all(build.parts.map(async part=>{
      const url=new URL(part.path,manifestPath);
      const response=await fetch(url,{cache:'no-store'});
      if(!response.ok) throw new Error(`${part.path}: HTTP ${response.status}`);
      const data=new Uint8Array(await response.arrayBuffer());
      if(!data.length) throw new Error(`${part.path}: file rỗng`);
      if(part.sha256){
        const digest=new Uint8Array(await crypto.subtle.digest('SHA-256',data));
        const hash=Array.from(digest,b=>b.toString(16).padStart(2,'0')).join('');
        if(hash!==part.sha256) throw new Error(`${part.path}: sai SHA-256, hãy tải lại trang`);
      }
      return {data,address:part.offset};
    }));
    const total=files.reduce((n,f)=>n+f.data.length,0);
    // All downloads and integrity checks finish BEFORE erasing anything.
    if(eraseFirst){
      phase='write_failed';
      emit('erasing','Đang xóa flash…',{done:false});
      await loader.eraseFlash();
      emit('erasing','Đã xóa flash',{done:true});
    }
    phase='write_failed';
    const progress=new Array(files.length).fill(0);
    await loader.writeFlash({fileArray:files,flashSize:'keep',flashMode:'keep',flashFreq:'keep',eraseAll:false,compress:true,
      reportProgress:(i,written,size)=>{
        progress[i]=size?Math.min(1,written/size)*files[i].data.length:0;
        const bytesWritten=progress.reduce((a,b)=>a+b,0);
        const percentage=Math.floor(bytesWritten/total*100);
        emit('writing',`Đang nạp firmware: ${percentage}%`,{bytesTotal:total,bytesWritten,percentage});
      }});
    emit('writing','Đã ghi xong firmware',{bytesTotal:total,bytesWritten:total,percentage:100});
    outcome=['finished','Nạp hoàn tất. Đang khởi động để cài Wi-Fi…',{}];
  } catch(error) {
    const message=error?.message || String(error);
    outcome=['error',`${phase==='failed_initialize'?'Không khởi tạo được ESP': 'Nạp chưa hoàn tất'}: ${message}`,{error:phase,details:message}];
  } finally {
    // The dialog reopens the port when it receives finished/error. Release first.
    await releaseTransport(transport,loader);
  }
  emit(...outcome);
}
