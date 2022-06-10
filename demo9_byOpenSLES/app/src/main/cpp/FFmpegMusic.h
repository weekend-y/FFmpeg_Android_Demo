//
// Created by weiqingyang on 2022/6/10.
//

#ifndef FFMPEG_BYOPENSLES_FFMPEGMUSIC_H
#define FFMPEG_BYOPENSLES_FFMPEGMUSIC_H

#include <jni.h>
#include <string>
#include <android/log.h>
extern "C" {
//编码
#include "libavcodec/avcodec.h"
//封装格式处理
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
//像素处理
#include "libswscale/swscale.h"
#include <android/native_window_jni.h>
#include <unistd.h>
}
#define LOGE(FORMAT,...) __android_log_print(ANDROID_LOG_ERROR,"weekend",FORMAT,##__VA_ARGS__);

int createFFmpeg(JNIEnv *env,int *rate,int *channel,jstring filename);

int getPcm(void **pcm,size_t *pcm_size);

void realseFFmpeg();

#endif //FFMPEG_BYOPENSLES_FFMPEGMUSIC_H
