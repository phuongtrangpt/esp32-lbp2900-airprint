// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.app.Activity;
import android.os.*;
import android.content.*;
import android.net.Uri;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.provider.Settings;
import android.view.*;
import android.widget.*;
import java.util.concurrent.*;

public final class MainActivity extends Activity {
 final Handler ui=new Handler(Looper.getMainLooper());final ExecutorService network=Executors.newSingleThreadExecutor();
 EditText ip;TextView status,history;boolean probing;
 int dp(int x){return (int)(getResources().getDisplayMetrics().density*x+.5f);}
 TextView text(String value,int size,int color){TextView t=new TextView(this);t.setText(value);t.setTextSize(size);t.setTextColor(color);t.setPadding(0,dp(8),0,dp(8));return t;}
 GradientDrawable background(int fill,int border){GradientDrawable d=new GradientDrawable();d.setColor(fill);d.setCornerRadius(dp(16));d.setStroke(dp(1),border);return d;}
 LinearLayout card(LinearLayout parent){LinearLayout c=new LinearLayout(this);c.setOrientation(1);c.setPadding(dp(20),dp(16),dp(20),dp(20));c.setBackground(background(0xff192a40,0xff48627d));c.setElevation(dp(5));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.setMargins(0,dp(16),0,dp(8));parent.addView(c,p);return c;}
 Button button(LinearLayout parent,String title,Runnable action){Button b=new Button(this);b.setText(title);b.setTextSize(15);b.setAllCaps(false);b.setTextColor(0xff172415);b.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xffc5f36b));b.setMinHeight(dp(52));LinearLayout.LayoutParams p=new LinearLayout.LayoutParams(-1,-2);p.topMargin=dp(12);parent.addView(b,p);b.setOnClickListener(v->action.run());return b;}
 @Override public void onCreate(Bundle state){super.onCreate(state);
  ScrollView scroll=new ScrollView(this);scroll.setFillViewport(true);scroll.setBackgroundColor(0xff0b1422);
  LinearLayout root=new LinearLayout(this);root.setOrientation(1);root.setPadding(dp(20),dp(24),dp(20),dp(24));scroll.addView(root);setContentView(scroll);
  scroll.setOnApplyWindowInsetsListener((v,insets)->{root.setPadding(dp(20),dp(20)+insets.getSystemWindowInsetTop(),dp(20),dp(20)+insets.getSystemWindowInsetBottom());return insets;});
  ImageView logo=new ImageView(this);logo.setImageResource(R.drawable.logo);logo.setContentDescription("Logo Việt Phương");root.addView(logo,new LinearLayout.LayoutParams(dp(70),dp(70)));
  TextView title=text("Viet Phuong\nPrint Connect",28,Color.WHITE);title.setTypeface(null,Typeface.BOLD);root.addView(title);
  root.addView(text("HP LaserJet P1006 · Android 0.1.0\nBản đầu tiên để kiểm thử trên máy thật",14,0xffb5c6db));
  LinearLayout config=card(root);config.addView(text("Kết nối ESP",21,Color.WHITE));config.addView(text("Điện thoại và ESP cần ở cùng mạng Wi-Fi. Dùng firmware Print Connect đã nạp cho bản Windows.",15,0xffb5c6db));
  TextView label=text("Địa chỉ IP của ESP",15,Color.WHITE);config.addView(label);ip=new EditText(this);ip.setSingleLine(true);ip.setHint("Ví dụ: 192.168.10.21");ip.setText(vn.io.vietphuong.printconnect.Settings.host(this));ip.setTextSize(18);ip.setTextColor(Color.WHITE);ip.setHintTextColor(0xff90a5bf);ip.setInputType(android.text.InputType.TYPE_CLASS_PHONE);ip.setPadding(dp(12),dp(12),dp(12),dp(12));ip.setBackground(background(0xff0d1929,0xff567390));config.addView(ip,new LinearLayout.LayoutParams(-1,-2));
  button(config,"Lưu IP và kiểm tra kết nối",()->{try{if(AppState.jobs.get()>0)throw new IllegalArgumentException("Chờ lệnh in xong trước khi đổi IP");vn.io.vietphuong.printconnect.Settings.save(this,ip.getText().toString());ip.clearFocus();probe();}catch(IllegalArgumentException e){status.setText(e.getMessage());}});
  status=text("Đang đọc kết nối…",16,0xffc5f36b);config.addView(status);
  LinearLayout setup=card(root);setup.addView(text("Bật dịch vụ in một lần",21,Color.WHITE));setup.addView(text("Mở cài đặt in của Android, bật “Viet Phuong · HP P1006”. Sau đó mở PDF, ảnh hoặc ứng dụng có mục In và chọn “HP P1006 - Viet Phuong”.",16,0xffc4d2e5));
  button(setup,"Mở cài đặt dịch vụ in Android",()->{try{startActivity(new Intent(Settings.ACTION_PRINT_SETTINGS));}catch(ActivityNotFoundException e){status.setText("Mở Cài đặt Android và tìm mục In / Printing");}});
  setup.addView(text("Không cần giữ giao diện này mở. Android quản lý dịch vụ in; không có dịch vụ chạy vô hạn khi máy đang rảnh.",14,0xffa9bbd3));
  LinearLayout progress=card(root);progress.addView(text("Lệnh in gần nhất",21,Color.WHITE));history=text(AppState.last(this),15,0xffc4d2e5);progress.addView(history);progress.addView(text("Đã chuyển USB không có nghĩa giấy đã ra hết. Nếu mất kết nối, kiểm tra giấy trước khi tạo lệnh mới để tránh in trùng.",14,0xffe8cc91));
  button(progress,"Mở trang in của ESP",()->{try{String host=vn.io.vietphuong.printconnect.Settings.checked(vn.io.vietphuong.printconnect.Settings.host(this));startActivity(new Intent(Intent.ACTION_VIEW,Uri.parse("http://"+host+"/")));}catch(Exception e){status.setText("Hãy lưu IP của ESP trước");}});
  root.addView(text("Việt Phương · GPL-3.0-or-later\nA4 / Letter / A5 / A6 · Đen trắng · Một mặt",13,0xff90a7c2));
 }
 final Runnable refresh=new Runnable(){public void run(){history.setText(AppState.last(MainActivity.this));probe();ui.postDelayed(this,5000);}};
 void probe(){if(probing||isDestroyed())return;if(AppState.jobs.get()>0){status.setText("Đang xử lý lệnh in · Xem tiến trình trong thông báo in Android");return;}probing=true;String host=vn.io.vietphuong.printconnect.Settings.host(this);network.execute(()->{DeviceStatus d=DeviceStatus.probe(host);ui.post(()->{probing=false;if(!isDestroyed())status.setText(d.text);});});}
 @Override protected void onResume(){super.onResume();ui.post(refresh);}
 @Override protected void onPause(){ui.removeCallbacks(refresh);super.onPause();}
 @Override protected void onDestroy(){ui.removeCallbacksAndMessages(null);network.shutdownNow();super.onDestroy();}
}
