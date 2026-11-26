// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import java.io.File;
public final class TransportHarness {
 public static void main(String[] args)throws Exception{
  Transport.Cancel cancel=new Transport.Cancel();
  if(args[0].equals("hello")){System.out.println(Transport.hello("127.0.0.1",Integer.parseInt(args[1])));return;}
  Transport.send("127.0.0.1",Integer.parseInt(args[1]),new File(args[2]),cancel,(done,total,msg)->{if(args[0].equals("cancel")&&done>0)cancel.cancel();});
 }
}
