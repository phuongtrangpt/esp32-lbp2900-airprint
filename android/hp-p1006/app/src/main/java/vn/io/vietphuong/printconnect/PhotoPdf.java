// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.graphics.*;
import android.media.ExifInterface;
import android.os.CancellationSignal;
import android.print.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Streaming photo PDF: decode one sampled image at a time; never hold a whole album. */
final class PhotoPdf {
 static final int MAX_PHOTOS=100;
 static void check(CancellationSignal cancel){cancel.throwIfCanceled();}
 static Matrix orientation(int exif){Matrix m=new Matrix();switch(exif){
  case 2:m.setScale(-1,1);break;case 3:m.setRotate(180);break;case 4:m.setScale(1,-1);break;
  case 5:m.setRotate(90);m.postScale(-1,1);break;case 6:m.setRotate(90);break;
  case 7:m.setRotate(-90);m.postScale(-1,1);break;case 8:m.setRotate(-90);break;
 }return m;}
 static Bitmap image(File file,CancellationSignal cancel)throws IOException{
  check(cancel);BitmapFactory.Options options=new BitmapFactory.Options();options.inJustDecodeBounds=true;BitmapFactory.decodeFile(file.getPath(),options);
  if(options.outWidth<1||options.outHeight<1)throw new IOException("Không đọc được ảnh. Hãy thử chia sẻ ảnh JPG hoặc PNG.");
  int sample=1;while((long)(options.outWidth/sample)*(options.outHeight/sample)>2_000_000L||Math.max(options.outWidth/sample,options.outHeight/sample)>3508)sample*=2;
  options.inJustDecodeBounds=false;options.inSampleSize=sample;options.inPreferredConfig=Bitmap.Config.ARGB_8888;
  Bitmap source=BitmapFactory.decodeFile(file.getPath(),options);if(source==null)throw new IOException("Ảnh không hợp lệ hoặc chưa tải về điện thoại");Bitmap rotated=null;
  try{
   check(cancel);int exif=1;try{exif=new ExifInterface(file.getPath()).getAttributeInt(ExifInterface.TAG_ORIENTATION,1);}catch(IOException ignored){}
   rotated=Bitmap.createBitmap(source,0,0,source.getWidth(),source.getHeight(),orientation(exif),true);
   Bitmap flat=Bitmap.createBitmap(rotated.getWidth(),rotated.getHeight(),Bitmap.Config.RGB_565);Canvas canvas=new Canvas(flat);canvas.drawColor(Color.WHITE);canvas.drawBitmap(rotated,0,0,new Paint(Paint.FILTER_BITMAP_FLAG));return flat;
  }finally{if(rotated!=null&&rotated!=source)rotated.recycle();source.recycle();}
 }
 static int[] pages(int count,PageRange[] ranges){return PdfPipeline.selected(count,ranges);}
 static String number(double d){return String.format(Locale.US,"%.3f",d);}
 static final class Output extends FilterOutputStream {
  long count;Output(OutputStream o){super(o);}
  @Override public void write(int value)throws IOException{out.write(value);count++;}
  @Override public void write(byte[] b,int start,int length)throws IOException{if(count+length>Transport.MAX)throw new IOException("PDF ảnh vượt giới hạn 256 MiB");out.write(b,start,length);count+=length;}
  void text(String s)throws IOException{byte[] b=s.getBytes(StandardCharsets.US_ASCII);write(b,0,b.length);}
 }
 static PageRange[] write(List<File> images,PrintAttributes attributes,PageRange[] requested,OutputStream destination,File cache,CancellationSignal cancel)throws IOException{
  int[] selected=pages(images.size(),requested);PrintAttributes.MediaSize media=attributes.getMediaSize();if(media==null)throw new IOException("Chưa chọn khổ giấy");
  double width=media.getWidthMils()*.072,height=media.getHeightMils()*.072;PrintAttributes.Margins margins=attributes.getMinMargins();
  double left=margins==null?0:margins.getLeftMils()*.072,right=margins==null?0:margins.getRightMils()*.072,top=margins==null?0:margins.getTopMils()*.072,bottom=margins==null?0:margins.getBottomMils()*.072;
  double availableW=width-left-right,availableH=height-top-bottom;if(availableW<=0||availableH<=0)throw new IOException("Lề giấy không hợp lệ");
  Output out=new Output(destination);long[] offsets=new long[3+3*selected.length];out.text("%PDF-1.4\n%VP-photo\n");
  offsets[1]=out.count;out.text("1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");
  offsets[2]=out.count;out.text("2 0 obj\n<< /Type /Pages /Count "+selected.length+" /Kids [");for(int i=0;i<selected.length;i++)out.text((3+i*3)+" 0 R ");out.text("] >>\nendobj\n");
  for(int i=0;i<selected.length;i++){
   check(cancel);Bitmap bitmap=image(images.get(selected[i]),cancel);File jpeg=null;int iw=bitmap.getWidth(),ih=bitmap.getHeight();
   try{
    jpeg=File.createTempFile("photo-page-",".jpg",cache);try(OutputStream jpg=new FileOutputStream(jpeg)){if(!bitmap.compress(Bitmap.CompressFormat.JPEG,94,jpg))throw new IOException("Không chuyển được ảnh");}
   }finally{bitmap.recycle();}
   try{
    check(cancel);int page=3+i*3,content=page+1,image=page+2;double scale=Math.min(availableW/iw,availableH/ih),w=iw*scale,h=ih*scale,x=left+(availableW-w)/2,y=bottom+(availableH-h)/2;
    offsets[page]=out.count;out.text(page+" 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 "+number(width)+" "+number(height)+"] /Resources << /XObject << /Im0 "+image+" 0 R >> >> /Contents "+content+" 0 R >>\nendobj\n");
    String drawing="q\n"+number(w)+" 0 0 "+number(h)+" "+number(x)+" "+number(y)+" cm\n/Im0 Do\nQ\n";
    offsets[content]=out.count;out.text(content+" 0 obj\n<< /Length "+drawing.getBytes(StandardCharsets.US_ASCII).length+" >>\nstream\n"+drawing+"endstream\nendobj\n");
    offsets[image]=out.count;out.text(image+" 0 obj\n<< /Type /XObject /Subtype /Image /Width "+iw+" /Height "+ih+" /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length "+jpeg.length()+" >>\nstream\n");
    try(InputStream in=new FileInputStream(jpeg)){byte[] bytes=new byte[65536];for(int n;(n=in.read(bytes))!=-1;){check(cancel);out.write(bytes,0,n);}}out.text("\nendstream\nendobj\n");
   }finally{if(jpeg!=null)jpeg.delete();}
  }
  check(cancel);long xref=out.count;out.text("xref\n0 "+offsets.length+"\n0000000000 65535 f \n");for(int i=1;i<offsets.length;i++)out.text(String.format(Locale.US,"%010d 00000 n \n",offsets[i]));
  out.text("trailer\n<< /Size "+offsets.length+" /Root 1 0 R >>\nstartxref\n"+xref+"\n%%EOF\n");out.flush();
  ArrayList<PageRange> ranges=new ArrayList<>();int start=selected[0],end=start;for(int i=1;i<selected.length;i++){if(selected[i]==end+1)end++;else{ranges.add(new PageRange(start,end));start=end=selected[i];}}ranges.add(new PageRange(start,end));return ranges.toArray(new PageRange[0]);
 }
}
