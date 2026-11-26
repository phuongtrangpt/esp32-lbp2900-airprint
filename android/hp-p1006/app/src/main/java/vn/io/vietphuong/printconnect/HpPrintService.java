// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.printservice.*;
import android.printservice.PrintJob;
import android.print.*;
import android.os.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;

public final class HpPrintService extends PrintService {
 final Handler main=new Handler(Looper.getMainLooper());
 final ExecutorService worker=Executors.newSingleThreadExecutor(),network=Executors.newSingleThreadExecutor();
 final Map<PrintJobId,Task> tasks=new HashMap<>();
 static final String ATTEMPTED="vp:usb-attempted";
 final class Task {final PrintJob job;final PrintJobId id;final Transport.Cancel cancel=new Transport.Cancel();final PrintJobInfo info;final String host;final ParcelFileDescriptor data;long nextStatus;
  Task(PrintJob j,ParcelFileDescriptor d){job=j;id=j.getId();info=j.getInfo();host=Settings.host(HpPrintService.this);data=d;}
 }
 @Override protected void onConnected(){
  // Android can restore an interrupted job. Never replay one that reached transport.
  for(PrintJob job:getActivePrintJobs())if(ATTEMPTED.equals(job.getTag()))job.fail("Lệnh trước bị gián đoạn. Kiểm tra giấy rồi tạo lệnh mới; không tự in lại.");else if(job.isQueued())onPrintJobQueued(job);
 }
 @Override protected void onPrintJobQueued(PrintJob job){
  if(tasks.containsKey(job.getId()))return;
  if(ATTEMPTED.equals(job.getTag())){job.fail("Kiểm tra giấy trước khi tạo lệnh mới; không tự gửi lại lệnh USB cũ.");return;}
  if(Settings.host(this).isEmpty()){job.fail("Mở Viet Phuong Print Connect để nhập IP của ESP");return;}
  if(!job.start())return;
  ParcelFileDescriptor data=job.getDocument().getData();if(data==null){job.fail("Android chưa cung cấp tài liệu PDF");return;}
  Task task=new Task(job,data);tasks.put(task.id,task);AppState.jobs.incrementAndGet();job.setStatus("Đang nhận tài liệu từ Android");worker.execute(()->run(task));
 }
 void status(Task t,float fraction,String message){long now=SystemClock.elapsedRealtime();if(now<t.nextStatus&&fraction<1)return;t.nextStatus=now+250;AppState.status(this,message);main.post(()->{if(tasks.containsKey(t.id)){t.job.setProgress(fraction);t.job.setStatus(message);}});}
 void run(Task t){
  File pdf=null,xqx=null;PowerManager.WakeLock awake=((PowerManager)getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK,"vp:print");boolean delivered=false;String error=null;
  try{
   awake.acquire(30*60*1000L);t.cancel.check();pdf=File.createTempFile("vp-", ".pdf",getCacheDir());xqx=new File(pdf.getPath()+".xqx");
   t.cancel.attach(t.data);
   try(InputStream in=new ParcelFileDescriptor.AutoCloseInputStream(t.data);OutputStream out=new FileOutputStream(pdf)){
    byte[] block=new byte[65536];long size=0;for(int n;(n=in.read(block))!=-1;){t.cancel.check();if((size+=n)>Transport.MAX)throw new IOException("PDF vượt giới hạn 256 MiB");out.write(block,0,n);}
   }finally{t.cancel.connection=null;}
   PrintAttributes attrs=t.info.getAttributes();if(attrs==null||attrs.getMediaSize()==null)throw new IOException("Thiếu thông tin khổ giấy");PrintAttributes.MediaSize media=attrs.getMediaSize();int paper=PdfPipeline.paper(media.getWidthMils(),media.getHeightMils());
   PdfPipeline.render(pdf,xqx,paper,!media.isPortrait(),t.info.getCopies(),t.info.getPages(),t.cancel,(done,total)->status(t,.45f*done/total,"Android đang xử lý và nén · "+done+"/"+total+" trang"));
   t.cancel.check();
   // Persist the no-replay marker in the Android spooler before opening a data session.
   CompletableFuture<Boolean> tagged=new CompletableFuture<>();main.post(()->{if(!tasks.containsKey(t.id)||t.cancel.flag.get())tagged.complete(false);else tagged.complete(t.job.setTag(ATTEMPTED));});
   if(!tagged.get(10,TimeUnit.SECONDS))throw new IOException("Không lưu được trạng thái an toàn của lệnh in");
   status(t,.45f,"Đang kết nối ESP và chuyển dữ liệu");Transport.send(t.host,9101,xqx,t.cancel,(done,total,msg)->status(t,.45f+.55f*done/total,msg+" · "+(done*100/total)+"%"));delivered=true;
  }catch(Exception|OutOfMemoryError e){error=e.getMessage()==null?"Không xử lý được lệnh in":e.getMessage();}
  finally{try{t.data.close();}catch(IOException ignored){}if(pdf!=null)pdf.delete();if(xqx!=null){xqx.delete();new File(xqx.getPath()+".tmp").delete();}if(awake.isHeld())awake.release();}
  boolean ok=delivered;String failure=error;main.post(()->{
   tasks.remove(t.id);AppState.jobs.decrementAndGet();
   if(ok){t.job.setProgress(1);t.job.setStatus("Đã chuyển đủ qua USB · Giấy có thể vẫn đang in");t.job.complete();AppState.status(this,"Đã chuyển đủ dữ liệu USB; chưa xác nhận giấy ra hết");}
   else if(t.cancel.flag.get()){t.job.cancel();AppState.status(this,"Đã hủy · Kiểm tra giấy trước khi in lại");}
   else{t.job.fail(failure);AppState.status(this,"Lệnh in gặp lỗi · "+failure);}
  });
 }
 @Override protected void onRequestCancelPrintJob(PrintJob job){Task t=tasks.get(job.getId());if(t!=null){t.cancel.cancel();job.setStatus("Đang hủy; một số trang có thể đã in");}else job.cancel();}
 @Override protected PrinterDiscoverySession onCreatePrinterDiscoverySession(){return new Discovery();}
 final class Discovery extends PrinterDiscoverySession {
  final PrinterId id=generatePrinterId("hp-p1006");boolean running,pending;
  PrinterCapabilitiesInfo capabilities(){return new PrinterCapabilitiesInfo.Builder(id).addMediaSize(PrintAttributes.MediaSize.ISO_A4,true).addMediaSize(PrintAttributes.MediaSize.NA_LETTER,false).addMediaSize(PrintAttributes.MediaSize.ISO_A5,false).addMediaSize(PrintAttributes.MediaSize.ISO_A6,false).addResolution(new PrintAttributes.Resolution("300","300 dpi",300,300),true).setColorModes(PrintAttributes.COLOR_MODE_MONOCHROME,PrintAttributes.COLOR_MODE_MONOCHROME).setDuplexModes(PrintAttributes.DUPLEX_MODE_NONE,PrintAttributes.DUPLEX_MODE_NONE).setMinMargins(new PrintAttributes.Margins(160,160,160,160)).build();}
  void publish(DeviceStatus s){if(isDestroyed())return;int code=s.code==0?PrinterInfo.STATUS_IDLE:s.code==1?PrinterInfo.STATUS_BUSY:PrinterInfo.STATUS_UNAVAILABLE;addPrinters(Collections.singletonList(new PrinterInfo.Builder(id,"HP P1006 - Viet Phuong",code).setDescription(s.text).setCapabilities(capabilities()).build()));}
  final Runnable tick=new Runnable(){public void run(){if(!running||isDestroyed())return;if(AppState.jobs.get()>0){publish(new DeviceStatus(1,"Đang xử lý lệnh in"));main.postDelayed(this,3000);return;}if(!pending){pending=true;String host=Settings.host(HpPrintService.this);network.execute(()->{DeviceStatus s=DeviceStatus.probe(host);main.post(()->{pending=false;if(running&&!isDestroyed())publish(s);});});}main.postDelayed(this,5000);}};
  @Override public void onStartPrinterDiscovery(List<PrinterId> priority){running=true;publish(new DeviceStatus(2,"Đang kiểm tra kết nối"));main.removeCallbacks(tick);main.post(tick);}
  @Override public void onStopPrinterDiscovery(){running=false;main.removeCallbacks(tick);}
  @Override public void onValidatePrinters(List<PrinterId> ids){if(running){main.removeCallbacks(tick);main.post(tick);}}
  @Override public void onStartPrinterStateTracking(PrinterId id){if(running){main.removeCallbacks(tick);main.post(tick);}}
  @Override public void onStopPrinterStateTracking(PrinterId id){}
  @Override public void onDestroy(){running=false;main.removeCallbacks(tick);}
 }
 @Override public void onDestroy(){for(Task t:tasks.values())t.cancel.cancel();worker.shutdown();network.shutdownNow();super.onDestroy();}
}
