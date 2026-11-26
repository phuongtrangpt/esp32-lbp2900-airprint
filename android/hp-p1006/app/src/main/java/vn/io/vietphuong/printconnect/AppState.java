// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.content.Context;
import java.text.DateFormat;
import java.util.Date;
import java.util.concurrent.atomic.AtomicInteger;
final class AppState {
 static final AtomicInteger jobs=new AtomicInteger();
 static volatile String message="Chưa có lệnh in";
 static void status(Context c,String text){message=text;c.getSharedPreferences("status",0).edit().putString("last",DateFormat.getTimeInstance().format(new Date())+" · "+text).apply();}
 static String last(Context c){return c.getSharedPreferences("status",0).getString("last","Chưa có lệnh in");}
}
