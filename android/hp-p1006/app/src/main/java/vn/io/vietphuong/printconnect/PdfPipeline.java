// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.graphics.*;
import android.graphics.pdf.PdfRenderer;
import android.os.ParcelFileDescriptor;
import android.print.PageRange;
import java.io.*;
import java.util.ArrayList;

final class PdfPipeline {
 static final int[][] PAPERS={{21000,29700},{21590,27940},{14800,21000},{10500,14800}};
 interface Progress {void update(int done,int total);}
 static int paper(int widthMils,int heightMils){
  int a=Math.min(widthMils,heightMils),b=Math.max(widthMils,heightMils);
  for(int i=0;i<PAPERS.length;i++)if(Math.abs(a*2.54-PAPERS[i][0])<80&&Math.abs(b*2.54-PAPERS[i][1])<80)return i;
  throw new IllegalArgumentException("Chỉ hỗ trợ A4, Letter, A5 và A6");
 }
 static int[] selected(int count,PageRange[] ranges){
  if(count<1||count>1000)throw new IllegalArgumentException("Tài liệu cần có 1–1000 trang");
  ArrayList<Integer> list=new ArrayList<>();
  for(int i=0;i<count;i++){boolean take=ranges==null||ranges.length==0; if(ranges!=null)for(PageRange r:ranges)take|=i>=r.getStart()&&i<=r.getEnd();if(take)list.add(i);}
  if(list.isEmpty())throw new IllegalArgumentException("Không có trang nào trong khoảng đã chọn");
  int[] result=new int[list.size()];for(int i=0;i<result.length;i++)result[i]=list.get(i);return result;
 }
 static int render(File pdf,File output,int paper,boolean landscape,int copies,PageRange[] ranges,Transport.Cancel cancel,Progress progress)throws IOException{
  if(paper<0||paper>=4||copies<1||copies>99)throw new IOException("Khổ giấy hoặc số bản không hợp lệ");
  File temp=new File(output.getPath()+".tmp");long handle=0;boolean finished=false;Bitmap bitmap=null;
  try(PdfRenderer renderer=new PdfRenderer(ParcelFileDescriptor.open(pdf,ParcelFileDescriptor.MODE_READ_ONLY))){
   int[] pages=selected(renderer.getPageCount(),ranges);if((long)pages.length*copies>1000)throw new IOException("Tối đa 1000 tờ mỗi lệnh");
   int width=PAPERS[paper][0]*300/2540,height=PAPERS[paper][1]*300/2540;
   bitmap=Bitmap.createBitmap(width,height,Bitmap.Config.ARGB_8888);
   handle=NativeEncoder.begin(temp.getPath(),paper);if(handle==0)throw new IOException("Không mở được bộ nén HP");
   int done=0,total=pages.length*copies;
   for(int copy=0;copy<copies;copy++)for(int index:pages){cancel.check();
    try(PdfRenderer.Page page=renderer.openPage(index)){
     bitmap.eraseColor(Color.WHITE);Matrix matrix=new Matrix();
     int logicalWidth=landscape?height:width,logicalHeight=landscape?width:height;
     matrix.setRectToRect(new RectF(0,0,page.getWidth(),page.getHeight()),new RectF(0,0,logicalWidth,logicalHeight),Matrix.ScaleToFit.CENTER);
     if(landscape){matrix.postRotate(90);matrix.postTranslate(width,0);}
     page.render(bitmap,null,matrix,PdfRenderer.Page.RENDER_MODE_FOR_PRINT);cancel.check();
     if(NativeEncoder.page(handle,bitmap)!=0)throw new IOException("Không nén được trang "+(index+1));
    }
    progress.update(++done,total);
   }
   cancel.check();long closing=handle;handle=0;if(NativeEncoder.end(closing,true)!=0)throw new IOException("Lệnh HP chưa đầy đủ");
   if(!temp.renameTo(output))throw new IOException("Không lưu được tài liệu đã xử lý");finished=true;return total;
  }finally{if(handle!=0)NativeEncoder.end(handle,false);if(bitmap!=null)bitmap.recycle();if(!finished)temp.delete();}
 }
}
