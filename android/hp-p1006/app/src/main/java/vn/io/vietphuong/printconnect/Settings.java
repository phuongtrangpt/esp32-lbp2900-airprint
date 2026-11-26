// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.content.*;
final class Settings {
 static String host(Context c){return c.getSharedPreferences("settings",0).getString("host","");}
 static String checked(String s){String[] a=s.trim().split("\\.",-1);if(a.length!=4)throw new IllegalArgumentException("Nhập IPv4 của ESP, ví dụ 192.168.10.21");for(String n:a){if(!n.matches("[0-9]{1,3}")||Integer.parseInt(n)>255)throw new IllegalArgumentException("Địa chỉ IPv4 không hợp lệ");}if(Integer.parseInt(a[0])==0||Integer.parseInt(a[0])>=224)throw new IllegalArgumentException("Cần IP của ESP trong mạng nội bộ");return String.join(".",java.util.Arrays.stream(a).map(n->Integer.toString(Integer.parseInt(n))).toArray(String[]::new));}
 static void save(Context c,String host){c.getSharedPreferences("settings",0).edit().putString("host",checked(host)).apply();}
}
