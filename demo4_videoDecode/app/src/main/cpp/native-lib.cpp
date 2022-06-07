#include <jni.h>
#include <stdio.h>
#include <iostream>
#include <string>
#include <android/log.h>
#ifdef __cplusplus
extern "C" {
#endif
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <unistd.h>
#ifdef __cplusplus
}
#endif

//过滤器宏开关
#define ABSFILTER_ENABLE    1

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"Weekend",FORMAT,##__VA_ARGS__);

bool video_decode(JNIEnv *env, jstring filename,jstring output)
{
    const char *inputPath = env->GetStringUTFChars(filename, JNI_FALSE);
    const char *outputPath = env->GetStringUTFChars(output, JNI_FALSE);

    FILE* fp_YUV = fopen(outputPath, "wb+");
    char buf[256];

    if (!fp_YUV){
        LOGE("open out.yuv failed");
        return false;
    }

    //注册各大组件
    av_register_all();
    LOGE("注册成功")
    AVFormatContext *avFormatContext = avformat_alloc_context();//获取上下文
    int error;
    //打开视频地址并获取里面的内容(解封装)
    error = avformat_open_input(&avFormatContext, inputPath, NULL, NULL);
    if (error < 0) {
        LOGE("打开视频失败")
        return false;
    }
    if (avformat_find_stream_info(avFormatContext, NULL) < 0) {
        LOGE("获取内容失败")
        return false;
    }

    //TODO
    av_dump_format(avFormatContext, 0, inputPath, 0);

    //获取视频的编码信息
    AVCodecParameters *origin_par = NULL;
    int mVideoStreamIdx = -1;
    mVideoStreamIdx = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (mVideoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find video stream in input file\n");
        return false;
    }
    LOGE("成功找到视频流")
    origin_par = avFormatContext->streams[mVideoStreamIdx]->codecpar;

    // 寻找解码器 {start
    AVCodec *mVcodec = NULL;
    AVCodecContext *mAvContext = NULL;
    mVcodec = avcodec_find_decoder(origin_par->codec_id);
    mAvContext = avcodec_alloc_context3(mVcodec);
    if (!mVcodec || !mAvContext) {
        return false;
    }

#if ABSFILTER_ENABLE
    //过滤器相关配置，这个与视频码流格式相关，针对mp4的不同格式，可能会需要增加这一块的过滤器，作为demo简单来说，可以需要也可以不需要
    const AVBitStreamFilter *absFilter = NULL;
    AVBSFContext *absCtx = NULL;
    AVCodecParameters *codecpar = NULL;
    absFilter = av_bsf_get_by_name("h264_mp4toannexb");
    //过滤器分配内存
    av_bsf_alloc(absFilter, &absCtx);
    //添加解码器属性
    codecpar = avFormatContext->streams[mVideoStreamIdx]->codecpar;
    avcodec_parameters_copy(absCtx->par_in, codecpar);
    absCtx->time_base_in = avFormatContext->streams[mVideoStreamIdx]->time_base;
    //初始化过滤器上下文
    av_bsf_init(absCtx);
#endif

    //不初始化解码器context会导致MP4封装的mpeg4码流解码失败
    int ret = avcodec_parameters_to_context(mAvContext, origin_par);
    if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Error initializing the decoder context.\n");
    }

    // 打开解码器
    if (avcodec_open2(mAvContext, mVcodec, NULL) != 0){
        LOGE("打开失败")
        return false;
    }
    LOGE("解码器打开成功")
    // 寻找解码器 end}


    //循环读入数据
    //申请AVPacket
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(packet);
    //申请AVFrame
    AVFrame *frame = av_frame_alloc();//分配一个AVFrame结构体,AVFrame结构体一般用于存储原始数据，指向解码后的原始帧

    uint8_t *byte_buffer = NULL;

    int byte_buffer_size = av_image_get_buffer_size(mAvContext->pix_fmt, mAvContext->width, mAvContext->height, 32);
    LOGE("width = %d , height = %d ",mAvContext->width, mAvContext->height);
    byte_buffer = (uint8_t*)av_malloc(byte_buffer_size);
    if (!byte_buffer) {
        av_log(NULL, AV_LOG_ERROR, "Can't allocate buffer\n");
        return AVERROR(ENOMEM);
    }

    while(1)
    {
        int ret = av_read_frame(avFormatContext, packet);
        if (ret != 0){
            av_strerror(ret,buf,sizeof(buf));
            LOGE("--%s--\n",buf);
            av_packet_unref(packet);
            break;
        }

        if (ret >= 0 && packet->stream_index != mVideoStreamIdx) {
            av_packet_unref(packet);
            continue;
        }

#if ABSFILTER_ENABLE
        if (av_bsf_send_packet(absCtx, packet) < 0) {
            LOGE("av_bsf_send_packet faile \n");
            av_packet_unref(packet);
            continue;
        }
        if (av_bsf_receive_packet(absCtx, packet) < 0) {
            LOGE("av_bsf_receive_packet faile \n");
            av_packet_unref(packet);
            continue;
        }
#endif

        {
            // 发送待解码包
            int result = avcodec_send_packet(mAvContext, packet);
            av_packet_unref(packet);
            if (result < 0) {
                av_log(NULL, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
                continue;
            }

            // 接收解码数据
            while (result >= 0){
                result = avcodec_receive_frame(mAvContext, frame);
                if (result == AVERROR_EOF)
                    break;
                else if (result == AVERROR(EAGAIN)) {
                    result = 0;
                    break;
                } else if (result < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
                    av_frame_unref(frame);
                    break;
                }

                int number_of_written_bytes = av_image_copy_to_buffer(byte_buffer, byte_buffer_size,
                                                                      (const uint8_t* const *)frame->data, (const int*) frame->linesize,
                                                                      mAvContext->pix_fmt, mAvContext->width, mAvContext->height, 1);
                if (number_of_written_bytes < 0) {
                    av_log(NULL, AV_LOG_ERROR, "Can't copy image to buffer\n");
                    av_frame_unref(frame);
                    continue;
                }

                // 写文件保存视频数据
                fwrite(byte_buffer, number_of_written_bytes, 1, fp_YUV);
                fflush(fp_YUV);

                av_frame_unref(frame);
            }
        }

    }

    fclose(fp_YUV);
    av_frame_free(&frame);
    avcodec_close(mAvContext);
    avformat_free_context(avFormatContext);
#if ABSFILTER_ENABLE
    av_bsf_free(&absCtx);
    absCtx = NULL;
#endif
    return true;
}


extern "C"
JNIEXPORT void JNICALL
Java_com_example_demo4_1videodecode_MainActivity_decode_1test(JNIEnv *env, jobject thiz,
                                                              jstring input_str, jint flag,
                                                              jstring output_str) {
    LOGE("begin to video decode");
    video_decode(env,input_str,output_str);
}