// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import java.io.*;
import java.net.*;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.CRC32;

final class Transport {
 static final byte[] REQUEST={'V','P','H','P','O','0','1','\n'},REPLY={'V','P','H','P','A','0','1','\n'};
 static final long MAX=256L*1024*1024;
 interface Progress {void update(long done,long total,String message);}
 static final class Cancel {
  final AtomicBoolean flag=new AtomicBoolean();volatile Closeable connection;
  void check() throws InterruptedIOException {if(flag.get())throw new InterruptedIOException("Đã hủy. Một số trang có thể đã in.");}
  synchronized void attach(Closeable s)throws IOException{if(flag.get()){s.close();check();}connection=s;}
  void cancel(){flag.set(true);Closeable s=connection;if(s!=null)try{s.close();}catch(IOException ignored){}}
 }
 static final class Reply {final int status,seq;final long accepted;Reply(int a,int b,long c){status=a;seq=b;accepted=c;}}
 static Socket connect(String host,int port,Cancel cancel)throws IOException{
  Socket s=new Socket();cancel.attach(s);try{s.connect(new InetSocketAddress(host,port),5000);s.setSoTimeout(1000);s.setTcpNoDelay(true);return s;}catch(IOException e){s.close();throw e;}
 }
 static void header(DataOutputStream out,int op,long total,int crc,long job)throws IOException{out.write(REQUEST);out.writeInt(op);out.writeLong(total);out.writeInt(crc);out.writeLong(job);out.flush();}
 static Reply reply(Socket s,Cancel cancel,long timeoutMs,Progress progress,long sent,long total)throws IOException{
  byte[] data=new byte[24];int pos=0;long deadline=System.nanoTime()+timeoutMs*1000000L,next=System.nanoTime()+5000000000L;
  InputStream in=s.getInputStream();
  while(pos<data.length){cancel.check();if(System.nanoTime()>deadline)throw new SocketTimeoutException("ESP chưa xác nhận USB; kiểm tra giấy trước khi in lại");
   try{int n=in.read(data,pos,data.length-pos);if(n<0)throw new EOFException("Mất kết nối ESP");pos+=n;}catch(SocketTimeoutException e){if(System.nanoTime()>next){progress.update(sent,total,"Đang chờ máy in nhận dữ liệu USB…");next=System.nanoTime()+5000000000L;}}
  }
  if(!Arrays.equals(REPLY,Arrays.copyOf(data,8)))throw new IOException("Sai giao thức: cần firmware Print Connect trên ESP");
  DataInputStream d=new DataInputStream(new ByteArrayInputStream(data,8,16));return new Reply(d.readInt(),d.readInt(),d.readLong());
 }
 static int hello(String host,int port)throws IOException{
  Cancel c=new Cancel();try(Socket s=connect(host,port,c)){header(new DataOutputStream(s.getOutputStream()),1,0,0,0);Reply r=reply(s,c,3000,(a,b,m)->{},0,0);if(r.seq!=0||r.accepted!=0)throw new IOException("Phản hồi trạng thái không hợp lệ");return r.status;}
 }
 static void send(String host,int port,File path,Cancel cancel,Progress progress)throws IOException{
  long total=path.length();if(total<128||total>MAX)throw new IOException("Dữ liệu in vượt giới hạn");
  CRC32 sum=new CRC32();byte[] block=new byte[4096];try(InputStream f=new FileInputStream(path)){for(int n;(n=f.read(block))!=-1;){cancel.check();sum.update(block,0,n);}}
  long job;do{job=new SecureRandom().nextLong();}while(job==0);
  long sent=0;int seq=0;
  try(Socket s=connect(host,port,cancel)){
   DataOutputStream out=new DataOutputStream(s.getOutputStream());header(out,2,total,(int)sum.getValue(),job);
   Reply initial=reply(s,cancel,150000,progress,0,total);
   if(initial.status!=0||initial.seq!=0||initial.accepted!=0)throw new IOException(initial.status==101?"Máy in đang bận; chưa gửi tài liệu":"ESP chưa sẵn sàng, mã "+initial.status);
   try(InputStream f=new FileInputStream(path)){
    while(sent<total){cancel.check();int n=(int)Math.min(block.length,total-sent);int got=0;while(got<n){int k=f.read(block,got,n-got);if(k<0)throw new EOFException("Tệp in không đầy đủ");got+=k;}
     CRC32 crc=new CRC32();crc.update(block,0,n);out.writeInt(seq);out.writeInt(n);out.writeInt((int)crc.getValue());out.write(block,0,n);out.flush();sent+=n;
     Reply ack=reply(s,cancel,150000,progress,sent,total);if(ack.status!=1||ack.seq!=seq||ack.accepted!=sent)throw new IOException("ESP từ chối khối USB "+seq+", mã "+ack.status);
     progress.update(sent,total,"Đang chuyển qua USB");seq++;
    }
   }
   cancel.check();out.writeInt(seq);out.writeInt(0);out.writeInt(0);out.flush();Reply end;
   try{end=reply(s,cancel,150000,progress,sent,total);}catch(IOException lost){
    cancel.check();try(Socket q=connect(host,port,cancel)){header(new DataOutputStream(q.getOutputStream()),3,0,0,job);end=reply(q,cancel,5000,progress,sent,total);}
    if(end.status!=2||end.accepted!=total)throw new IOException("Chưa xác định kết quả USB. Kiểm tra giấy trước khi tạo lệnh mới",lost);return;
   }
   if(end.status!=2||end.seq!=seq||end.accepted!=total)throw new IOException("ESP từ chối kết thúc lệnh in");
  }catch(IOException e){if(sent>0)throw new IOException(e.getMessage()+". Không tự gửi lại; một số trang có thể đã in.",e);throw e;}
  finally{cancel.connection=null;}
 }
}
