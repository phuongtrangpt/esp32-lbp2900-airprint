export function createProgress(){return {visible:false,active:false,status:'Chờ bắt đầu',percent:null,bytes:'',lines:[],lastBucket:-1};}
export function updateProgress(previous,event,time){
  let s={...previous,lines:[...previous.lines]};
  if(event.state==='initializing'&&!s.active){s=createProgress();s.visible=true;s.active=true;}
  s.visible=true;
  s.status=event.message;
  const d=event.details||{};
  let log=true;
  if(event.state==='writing'&&Number.isFinite(d.percentage)){
    s.percent=Math.max(0,Math.min(100,d.percentage));
    s.bytes=Number.isFinite(d.bytesWritten)&&Number.isFinite(d.bytesTotal)?`${(d.bytesWritten/1048576).toFixed(2)} / ${(d.bytesTotal/1048576).toFixed(2)} MB`:'';
    const bucket=Math.floor(s.percent/5);
    log=bucket!==s.lastBucket||event.message==='Đã ghi xong firmware';s.lastBucket=bucket;
  }
  if(event.state==='finished'||event.state==='error')s.active=false;
  s.state=event.state;
  if(log&&s.lines.at(-1)?.message!==event.message)s.lines.push({time,message:event.message,error:event.state==='error'});
  s.lines=s.lines.slice(-120);
  return s;
}
