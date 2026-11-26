// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.test.InstrumentationTestCase;
import android.graphics.*;
import android.graphics.pdf.PdfDocument;
import android.print.PageRange;
import java.io.*;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public final class PipelineTest extends InstrumentationTestCase {
 File pdf(int count,boolean landscape)throws Exception{
  File f=File.createTempFile("input", ".pdf",getInstrumentation().getTargetContext().getCacheDir());
  try(PdfDocument d=new PdfDocument();OutputStream out=new FileOutputStream(f)){
   for(int i=0;i<count;i++){PdfDocument.Page p=d.startPage(new PdfDocument.PageInfo.Builder(landscape?420:297,landscape?297:420,i+1).create());Paint paint=new Paint();paint.setColor(Color.BLACK);paint.setTextSize(20);p.getCanvas().drawText("Page "+(i+1),30,60,paint);p.getCanvas().drawRect(35,80,80+i,100,paint);d.finishPage(p);}d.writeTo(out);
  }return f;
 }
 int pageRecords(File file)throws Exception{
  byte[] data;try(InputStream in=new FileInputStream(file);ByteArrayOutputStream out=new ByteArrayOutputStream()){byte[] block=new byte[8192];for(int n;(n=in.read(block))!=-1;)out.write(block,0,n);data=out.toByteArray();}
  byte[] magic=",XQX".getBytes("US-ASCII");int pos=-1;for(int i=0;i<data.length-4;i++)if(data[i]==magic[0]&&data[i+1]==magic[1]&&data[i+2]==magic[2]&&data[i+3]==magic[3]){pos=i+4;break;}assertTrue(pos>0);
  ByteBuffer b=ByteBuffer.wrap(data).order(ByteOrder.BIG_ENDIAN);b.position(pos);int pages=0,ends=0;
  while(b.remaining()>=8){int type=b.getInt(),n=b.getInt();if(type==7){assertTrue(n>=0&&n<=b.remaining());b.position(b.position()+n);}else{for(int i=0;i<n;i++){b.getInt();int size=b.getInt();assertTrue(size>=0&&size<=b.remaining());b.position(b.position()+size);}}
   if(type==3)pages++;if(type==4)ends++;if(type==2)break;
  }assertEquals(pages,ends);return pages;
 }
 void convert(int count,int paper,boolean landscape,int copies,PageRange[] ranges,int expected)throws Exception{
  File input=pdf(count,landscape),out=new File(input.getPath()+".xqx");
  try{assertEquals(expected,PdfPipeline.render(input,out,paper,landscape,copies,ranges,new Transport.Cancel(),(a,b)->{}));assertEquals(expected,pageRecords(out));assertFalse(new File(out.getPath()+".tmp").exists());}finally{input.delete();out.delete();}
 }
 public void testFortyPagesA6()throws Exception{convert(40,3,false,1,new PageRange[]{PageRange.ALL_PAGES},40);}
 public void testSelectedFiveThroughTen()throws Exception{convert(40,3,false,1,new PageRange[]{new PageRange(4,9)},6);}
 public void testCopiesAndLandscapeA5()throws Exception{convert(3,2,true,2,new PageRange[]{new PageRange(1,2)},4);}
 public void testA4AndLetter()throws Exception{convert(1,0,false,1,null,1);convert(1,1,false,1,null,1);}
 public void testCanceledBeforeRenderSendsNothing()throws Exception{File input=pdf(2,false),out=new File(input.getPath()+".xqx");Transport.Cancel c=new Transport.Cancel();c.cancel();try{PdfPipeline.render(input,out,3,false,1,null,c,(a,b)->{});fail("Expected cancel");}catch(InterruptedIOException expected){}finally{input.delete();}assertFalse(out.exists());assertFalse(new File(out.getPath()+".tmp").exists());}
}
