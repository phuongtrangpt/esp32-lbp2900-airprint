// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.app.*;
import android.content.*;
import android.net.Uri;
import android.os.*;
import android.print.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.util.*;
import java.util.concurrent.*;

/** Explicit Share receiver and SAF picker; reads only content URIs granted by the user. */
public final class PhotoShareActivity extends Activity {
 final ExecutorService worker=Executors.newSingleThreadExecutor();final Handler main=new Handler(Looper.getMainLooper());final List<File> images=new ArrayList<>();volatile boolean closed;volatile InputStream reading;File cache;TextView status;Button print;boolean handedToPrinter;
 static List<Uri> uris(Intent intent){
  LinkedHashSet<Uri> result=new LinkedHashSet<>();
  if(Intent.ACTION_SEND_MULTIPLE.equals(intent.getAction())){ArrayList<Uri> list=intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);if(list!=null)result.addAll(list);}
  else{Uri single=intent.getParcelableExtra(Intent.EXTRA_STREAM);if(single!=null)result.add(single);}
  if(intent.getClipData()!=null)for(int i=0;i<intent.getClipData().getItemCount();i++){Uri uri=intent.getClipData().getItemAt(i).getUri();if(uri!=null)result.add(uri);}
  if(result.isEmpty()&&intent.getData()!=null)result.add(intent.getData());
  if(result.size()>PhotoPdf.MAX_PHOTOS)throw new IllegalArgumentException("Tối đa 100 ảnh mỗi lệnh");
  for(Uri uri:result)if(uri==null||!"content".equals(uri.getScheme()))throw new IllegalArgumentException("Chỉ nhận ảnh được chia sẻ từ Thư viện hoặc trình chọn ảnh");return new ArrayList<>(result);
 }
 TextView text(String s,int size){TextView t=new TextView(this);t.setText(s);t.setTextSize(size);t.setTextColor(0xffe6efff);t.setPadding(0,12,0,12);return t;}
 @Override public void onCreate(Bundle state){super.onCreate(state);
  LinearLayout panel=new LinearLayout(this);panel.setOrientation(1);panel.setPadding(28,32,28,32);panel.setBackgroundColor(0xff142238);ScrollView scroll=new ScrollView(this);scroll.addView(panel);setContentView(scroll);
  scroll.setOnApplyWindowInsetsListener((v,insets)->{panel.setPadding(28,28+insets.getSystemWindowInsetTop(),28,28+insets.getSystemWindowInsetBottom());return insets;});
  panel.addView(text("Viet Phuong Print Connect",25));panel.addView(text("In ảnh với HP P1006",21));status=text("Đang nhận ảnh…",17);panel.addView(status);
  panel.addView(text("Mỗi ảnh một trang · Vừa khổ giấy · Giữ chiều ảnh\nTrong hộp thoại tiếp theo, chọn HP P1006 - Viet Phuong, rồi chọn A4/A5/A6, số bản và hướng giấy.",16));
  print=new Button(this);print.setText("Mở hộp thoại in ảnh");print.setAllCaps(false);print.setEnabled(false);panel.addView(print);print.setOnClickListener(v->startPrint());
  Button back=new Button(this);back.setText("Quay lại");back.setAllCaps(false);panel.addView(back);back.setOnClickListener(v->finish());
  try{cache=File.createTempFile("vp-photos-","",getCacheDir());if(!cache.delete()||!cache.mkdir())throw new IOException("Không tạo được thư mục ảnh tạm");
   if(Intent.ACTION_SEND.equals(getIntent().getAction())||Intent.ACTION_SEND_MULTIPLE.equals(getIntent().getAction()))load(uris(getIntent()));
   else if(state==null){Intent choose=new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("image/*").addCategory(Intent.CATEGORY_OPENABLE).putExtra(Intent.EXTRA_ALLOW_MULTIPLE,true);startActivityForResult(choose,37);}
  }catch(Exception e){status.setText("Không nhận được ảnh: "+e.getMessage());}
 }
 @Override protected void onActivityResult(int request,int result,Intent data){super.onActivityResult(request,result,data);if(request==37){if(result!=RESULT_OK||data==null){finish();return;}try{load(uris(data));}catch(Exception e){status.setText(e.getMessage());}}}
 void load(List<Uri> sources){if(sources.isEmpty()){status.setText("Không có ảnh được chia sẻ");return;}worker.execute(()->{
  try{long total=0;for(Uri uri:sources){if(closed)return;File file=File.createTempFile("photo-",".image",cache);long one=0;
    try(InputStream in=getContentResolver().openInputStream(uri);OutputStream out=new FileOutputStream(file)){if(in==null)throw new IOException("Không mở được ảnh");reading=in;byte[] block=new byte[65536];for(int n;(n=in.read(block))!=-1;){if(closed)return;one+=n;total+=n;if(one>64L*1024*1024||total>Transport.MAX)throw new IOException("Ảnh quá lớn; tối đa 64 MiB/ảnh, 256 MiB mỗi lệnh");out.write(block,0,n);}}
    finally{reading=null;}images.add(file);
   }
   main.post(()->{if(!closed){status.setText("Đã nhận "+images.size()+" ảnh. Sẵn sàng chọn máy in.");print.setEnabled(true);}});
  }catch(Exception e){main.post(()->{if(!closed)status.setText("Không nhận được ảnh: "+e.getMessage());});}
 });}
 void startPrint(){if(images.isEmpty()||closed)return;print.setEnabled(false);try{
  PrintAttributes attrs=new PrintAttributes.Builder().setMediaSize(PrintAttributes.MediaSize.ISO_A4).setResolution(new PrintAttributes.Resolution("300","300 dpi",300,300)).setColorMode(PrintAttributes.COLOR_MODE_MONOCHROME).setMinMargins(new PrintAttributes.Margins(160,160,160,160)).build();
  PhotoPrintAdapter adapter=new PhotoPrintAdapter(images,cache,()->{handedToPrinter=false;cleanup();finish();});((PrintManager)getSystemService(PRINT_SERVICE)).print("Ảnh HP P1006",adapter,attrs);handedToPrinter=true;
 }catch(Exception e){status.setText("Không mở được hộp thoại in: "+e.getMessage());print.setEnabled(true);}}
 void cleanup(){if(cache==null)return;File[] files=cache.listFiles();if(files!=null)for(File f:files)f.delete();cache.delete();}
 @Override protected void onDestroy(){closed=true;InputStream in=reading;if(in!=null)try{in.close();}catch(IOException ignored){}if(!handedToPrinter)worker.execute(this::cleanup);worker.shutdown();super.onDestroy();}
}
