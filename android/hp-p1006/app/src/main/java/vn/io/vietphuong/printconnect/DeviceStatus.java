// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import java.net.*;
import java.io.*;
import org.json.*;
final class DeviceStatus {
 final int code;final String text;
 DeviceStatus(int code,String text){this.code=code;this.text=text;}
 static DeviceStatus probe(String host){
  if(host.isEmpty())return new DeviceStatus(2,"Chưa nhập IP của ESP");
  HttpURLConnection c=null;
  try{
   c=(HttpURLConnection)new URL("http://"+host+"/api/state").openConnection();c.setConnectTimeout(1800);c.setReadTimeout(1800);c.setInstanceFollowRedirects(false);
   if(c.getResponseCode()==200){ByteArrayOutputStream body=new ByteArrayOutputStream();byte[] b=new byte[1024];try(InputStream in=c.getInputStream()){for(int n;(n=in.read(b))!=-1;){if(body.size()+n>65536)throw new IOException("Status too large");body.write(b,0,n);}}
    JSONObject p=new JSONObject(body.toString("UTF-8")).getJSONObject("printer");
    if(!p.optBoolean("connected")||!p.optString("model").toLowerCase(java.util.Locale.ROOT).contains("p1006"))return new DeviceStatus(2,"ESP chưa nhận đúng HP P1006 qua USB");
    if("processing".equals(p.optString("state")))return new DeviceStatus(1,"Đã kết nối · Máy in đang nhận lệnh");
    JSONArray reasons=p.optJSONArray("reasons");if(reasons!=null)for(int i=0;i<reasons.length();i++)if(reasons.optString(i).endsWith("-error"))return new DeviceStatus(2,"Máy in cần kiểm tra: "+p.optString("message","lỗi thiết bị"));
   }
   return hello(host);
  }catch(Exception e){try{return hello(host);}catch(Exception ignored){return new DeviceStatus(2,"Offline · Kiểm tra Wi-Fi, nguồn ESP và địa chỉ IP");}}
  finally{if(c!=null)c.disconnect();}
 }
 private static DeviceStatus hello(String host)throws IOException{int s=Transport.hello(host,9101);return s==0?new DeviceStatus(0,"Đã kết nối HP P1006"):s==101?new DeviceStatus(1,"Đã kết nối · Máy in đang bận"):new DeviceStatus(2,"ESP chưa sẵn sàng · mã "+s);}
}
