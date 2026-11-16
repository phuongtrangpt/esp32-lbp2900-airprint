const sleep=ms=>new Promise(resolve=>setTimeout(resolve,ms));

export async function releaseTransport(transport,loader) {
  if(!transport) return;
  try {
    if(loader?.chip){
      await transport.setDTR(false);
      await transport.setRTS(true);
      await sleep(100);
      await loader.after();
    }
  } catch { /* A disconnected USB cable must not prevent releasing the reader. */ }
  try { await transport.disconnect(); } catch { /* Already disconnected. */ }
}

// Retry only connection, never erase/write. A fresh loader cannot inherit stale stub state.
export async function connectLoader(port,{Transport,ESPLoader},report) {
  let lastError;
  const attempts=[
    {mode:'default_reset',label:'Đang tự động kết nối…'},
    {mode:'default_reset',legacy:true,label:'Đang thử reset UART tương thích…'},
    {mode:'no_reset',label:'Đang thử chế độ BOOT thủ công: giữ BOOT, bấm rồi thả RESET…'},
  ];
  for(const attempt of attempts){
    const transport=new Transport(port);
    const options={transport,baudrate:115200,enableTracing:false};
    if(attempt.legacy) options.resetConstructors={classicReset:(t,delay)=>({async reset(){
      // Separate DTR/RTS writes support bridges which mishandle simultaneous signals.
      await t.setDTR(false); await t.setRTS(true); await sleep(100);
      await t.setDTR(true); await t.setRTS(false); await sleep(delay); await t.setDTR(false);
    }})};
    const loader=new ESPLoader(options);
    report(attempt.label);
    try {
      await loader.main(attempt.mode);
      await loader.flashId();
      return {transport,loader};
    } catch(error){
      lastError=error;
      report(`${attempt.label} ${error?.message || error}`);
      // Preserve current boot mode so no_reset can attach to a manually booted board.
      await releaseTransport(transport);
      if(error?.name==='NotFoundError' || error?.name==='NetworkError') break;
    }
  }
  throw lastError;
}
