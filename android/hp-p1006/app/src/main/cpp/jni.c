// SPDX-License-Identifier: GPL-3.0-or-later
#include <jni.h>
#include <android/bitmap.h>
#include <stdint.h>
#include "encoder.h"
JNIEXPORT jlong JNICALL Java_vn_io_vietphuong_printconnect_NativeEncoder_begin(JNIEnv *env,jclass c,jstring path,jint paper){(void)c;const char *p=(*env)->GetStringUTFChars(env,path,NULL);if(!p)return 0;vp_encoder *e=vp_begin(p,paper);(*env)->ReleaseStringUTFChars(env,path,p);return (jlong)(intptr_t)e;}
JNIEXPORT jint JNICALL Java_vn_io_vietphuong_printconnect_NativeEncoder_page(JNIEnv *env,jclass c,jlong ptr,jobject bitmap){(void)c;AndroidBitmapInfo info;void *pixels;if(AndroidBitmap_getInfo(env,bitmap,&info)||info.format!=ANDROID_BITMAP_FORMAT_RGBA_8888||AndroidBitmap_lockPixels(env,bitmap,&pixels))return -1;int rc=vp_page((vp_encoder *)(intptr_t)ptr,pixels,info.width,info.height,info.stride);AndroidBitmap_unlockPixels(env,bitmap);return rc;}
JNIEXPORT jint JNICALL Java_vn_io_vietphuong_printconnect_NativeEncoder_end(JNIEnv *env,jclass c,jlong ptr,jboolean ok){(void)env;(void)c;return vp_end((vp_encoder *)(intptr_t)ptr,ok);}
