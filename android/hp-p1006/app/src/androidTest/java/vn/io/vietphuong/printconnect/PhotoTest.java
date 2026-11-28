// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.*;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;
import android.content.*;
import android.graphics.*;
import android.graphics.pdf.PdfRenderer;
import android.media.ExifInterface;
import android.net.Uri;
import android.os.*;
import android.print.*;
import java.io.*;
import java.util.*;

@RunWith(AndroidJUnit4.class)
public final class PhotoTest {
 File dir;
 @Before public void setup()throws Exception{dir=File.createTempFile("photos-test-","",InstrumentationRegistry.getInstrumentation().getTargetContext().getCacheDir());assertTrue(dir.delete());assertTrue(dir.mkdir());}
 @After public void cleanup(){File[] files=dir.listFiles();if(files!=null)for(File file:files)file.delete();dir.delete();}
 File photo(String name,boolean alpha)throws Exception{
  Bitmap b=Bitmap.createBitmap(120,80,Bitmap.Config.ARGB_8888);b.eraseColor(alpha?Color.TRANSPARENT:Color.BLUE);Canvas c=new Canvas(b);Paint p=new Paint();p.setColor(Color.RED);c.drawRect(0,0,60,80,p);File file=new File(dir,name);
  try(OutputStream out=new FileOutputStream(file)){assertTrue(b.compress(alpha?Bitmap.CompressFormat.PNG:Bitmap.CompressFormat.JPEG,98,out));}finally{b.recycle();}return file;
 }
 PrintAttributes attrs(PrintAttributes.MediaSize media){return new PrintAttributes.Builder().setMediaSize(media).setMinMargins(new PrintAttributes.Margins(160,160,160,160)).setColorMode(PrintAttributes.COLOR_MODE_MONOCHROME).setResolution(new PrintAttributes.Resolution("300","300 dpi",300,300)).build();}
 File write(List<File> photos,PageRange[] ranges,int expected)throws Exception{
  File pdf=new File(dir,"out.pdf");try(OutputStream out=new FileOutputStream(pdf)){PageRange[] selected=PhotoPdf.write(photos,attrs(PrintAttributes.MediaSize.ISO_A5),ranges,out,dir,new CancellationSignal());assertNotNull(selected);}
  try(PdfRenderer renderer=new PdfRenderer(ParcelFileDescriptor.open(pdf,ParcelFileDescriptor.MODE_READ_ONLY))){assertEquals(expected,renderer.getPageCount());Bitmap b=Bitmap.createBitmap(420,595,Bitmap.Config.ARGB_8888);b.eraseColor(Color.WHITE);try(PdfRenderer.Page page=renderer.openPage(0)){page.render(b,null,null,PdfRenderer.Page.RENDER_MODE_FOR_DISPLAY);}assertNotEquals(Color.WHITE,b.getPixel(150,297));b.recycle();}return pdf;
 }
 @Test public void singleImageAndNativeOutput()throws Exception{
  File input=photo("one.png",true),pdf=write(Collections.singletonList(input),null,1),xqx=new File(dir,"out.xqx");assertEquals(1,PdfPipeline.render(pdf,xqx,2,false,1,null,new Transport.Cancel(),(a,b)->{}));assertTrue(xqx.length()>128);
 }
 @Test public void selectedMultipleImages()throws Exception{File input=photo("image.jpg",false);write(Arrays.asList(input,input,input,input),new PageRange[]{new PageRange(1,2)},2);}
 @Test public void hundredImagesStreamWithoutAlbumBitmaps()throws Exception{File input=photo("image.jpg",false);write(Collections.nCopies(100,input),null,100);}
 @Test public void exifRotationAndTransparency()throws Exception{
  File jpeg=photo("rotate.jpg",false);ExifInterface exif=new ExifInterface(jpeg.getPath());exif.setAttribute(ExifInterface.TAG_ORIENTATION,"6");exif.saveAttributes();Bitmap rotated=PhotoPdf.image(jpeg,new CancellationSignal());assertEquals(80,rotated.getWidth());assertEquals(120,rotated.getHeight());assertTrue(Color.red(rotated.getPixel(20,20))>200);assertTrue(Color.blue(rotated.getPixel(20,100))>200);rotated.recycle();
  Bitmap flat=PhotoPdf.image(photo("alpha.png",true),new CancellationSignal());assertEquals(Color.WHITE,flat.getPixel(100,40));flat.recycle();
 }
 @Test public void shareUriOrderAndDedup(){
  Uri a=Uri.parse("content://media/1"),b=Uri.parse("content://media/2");Intent i=new Intent(Intent.ACTION_SEND_MULTIPLE).setType("image/*").putParcelableArrayListExtra(Intent.EXTRA_STREAM,new ArrayList<>(Arrays.asList(a,b)));ClipData clip=ClipData.newRawUri("photos",a);clip.addItem(new ClipData.Item(b));i.setClipData(clip);assertEquals(Arrays.asList(a,b),PhotoShareActivity.uris(i));
 }
 @Test public void shareTargetResolvesForJpegAndMultiple(){
  Context c=InstrumentationRegistry.getInstrumentation().getTargetContext();for(String action:new String[]{Intent.ACTION_SEND,Intent.ACTION_SEND_MULTIPLE}){Intent i=new Intent(action).setType("image/jpeg").setPackage(c.getPackageName());assertTrue(c.getPackageManager().queryIntentActivities(i,0).stream().anyMatch(r->r.activityInfo.name.endsWith("PhotoShareActivity")));}
 }
 @Test public void cancelAndRejectUnsharedFile()throws Exception{
  CancellationSignal cancel=new CancellationSignal();cancel.cancel();try{PhotoPdf.image(photo("image.jpg",false),cancel);fail("cancel expected");}catch(OperationCanceledException expected){}
  try{PhotoShareActivity.uris(new Intent(Intent.ACTION_SEND).putExtra(Intent.EXTRA_STREAM,Uri.parse("file:///private/photo.jpg")));fail("content URI required");}catch(IllegalArgumentException expected){}
 }
}
