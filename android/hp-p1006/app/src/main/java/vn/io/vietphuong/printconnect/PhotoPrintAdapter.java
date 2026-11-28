// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.print.*;
import android.os.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;

final class PhotoPrintAdapter extends PrintDocumentAdapter {
 final List<File> photos;final File cache;final Runnable finished;final Handler main=new Handler(Looper.getMainLooper());final ExecutorService worker=Executors.newSingleThreadExecutor();PrintAttributes attributes;
 PhotoPrintAdapter(List<File> files,File cache,Runnable finished){this.photos=new ArrayList<>(files);this.cache=cache;this.finished=finished;}
 @Override public void onLayout(PrintAttributes old,PrintAttributes next,CancellationSignal cancel,LayoutResultCallback callback,Bundle extras){
  if(cancel.isCanceled()){callback.onLayoutCancelled();return;}attributes=next;callback.onLayoutFinished(new PrintDocumentInfo.Builder("Ảnh HP P1006").setContentType(PrintDocumentInfo.CONTENT_TYPE_PHOTO).setPageCount(photos.size()).build(),!next.equals(old));
 }
 @Override public void onWrite(PageRange[] pages,ParcelFileDescriptor target,CancellationSignal cancel,WriteResultCallback callback){
  PrintAttributes snapshot=attributes;worker.execute(()->{
   try(OutputStream out=new ParcelFileDescriptor.AutoCloseOutputStream(target)){
    PageRange[] written=PhotoPdf.write(photos,snapshot,pages,out,cache,cancel);main.post(()->{if(cancel.isCanceled())callback.onWriteCancelled();else callback.onWriteFinished(written);});
   }catch(OperationCanceledException e){main.post(callback::onWriteCancelled);}catch(Exception|OutOfMemoryError e){String reason=e.getMessage();main.post(()->callback.onWriteFailed(reason==null?"Không tạo được PDF từ ảnh":reason));}
  });
 }
 @Override public void onFinish(){worker.shutdown();finished.run();}
}
