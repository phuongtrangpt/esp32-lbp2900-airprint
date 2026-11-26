// SPDX-License-Identifier: GPL-3.0-or-later
package vn.io.vietphuong.printconnect;
import android.graphics.Bitmap;
final class NativeEncoder {
 static { System.loadLibrary("vp_hp"); }
 static native long begin(String path,int paper);
 static native int page(long handle,Bitmap bitmap);
 static native int end(long handle,boolean success);
}
