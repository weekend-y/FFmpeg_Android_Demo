#include <jni.h>
#include <string>
#include <android/log.h>

extern "C"{
//编码
#include "libavcodec/avcodec.h"
//封装格式处理
#include "libavformat/avformat.h"
//像素处理
#include "libswscale/swscale.h"
#include <android/native_window_jni.h>
#include <unistd.h>
}

//过滤器宏开关
#define ABSFILTER_ENABLE    0

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"Weekend",FORMAT,##__VA_ARGS__);

extern "C"
JNIEXPORT void JNICALL
Java_com_example_mydemo3_1videoplay2_MainActivity_render(JNIEnv *env, jobject thiz,
                                                        jstring input_str, jobject surface) {
    const char *inputPath = env->GetStringUTFChars(input_str, JNI_FALSE);

    //注册各大组件
    av_register_all();
    LOGE("注册成功")
    AVFormatContext *avFormatContext = avformat_alloc_context();//获取上下文
    int error;
    char buf[] = "";
    //打开视频地址并获取里面的内容(解封装)
    error = avformat_open_input(&avFormatContext, inputPath, NULL, NULL);
    if (error < 0) {
        av_strerror(error, buf, 1024);
        LOGE("Couldn't open file %s: %d(%s)", inputPath, error, buf);
        LOGE("打开视频失败")
        return;
    }
    if (avformat_find_stream_info(avFormatContext, NULL) < 0) {
        LOGE("获取内容失败")
        return;
    }

    //TODO
    av_dump_format(avFormatContext, 0, inputPath, 0);

    //获取视频的编码信息
    AVCodecParameters *origin_par = NULL;
    int mVideoStreamIdx = -1;
    mVideoStreamIdx = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (mVideoStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find video stream in input file\n");
        return;
    }
    LOGE("成功找到视频流")
    origin_par = avFormatContext->streams[mVideoStreamIdx]->codecpar;

    // 寻找解码器 {start
    AVCodec *mVcodec = NULL;
    AVCodecContext *mAvContext = NULL;
    mVcodec = avcodec_find_decoder(origin_par->codec_id);
    mAvContext = avcodec_alloc_context3(mVcodec);
    if (!mVcodec || !mAvContext) {
        return;
    }

#if ABSFILTER_ENABLE
    //过滤器相关配置，这个与视频码流格式相关，针对mp4的不同格式，可能会需要增加这一块的过滤器，目前作为demo简单来说，可以不需要，先屏蔽，需要可以配置宏打开
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
        return;
    }
    LOGE("解码器打开成功")
    // 寻找解码器 end}


    //申请AVPacket
    AVPacket *packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(packet);
    //申请AVFrame
    AVFrame *frame = av_frame_alloc();//分配一个AVFrame结构体,AVFrame结构体一般用于存储原始数据，指向解码后的原始帧
    AVFrame *rgb_frame = av_frame_alloc();//分配一个AVFrame结构体，指向存放转换成rgb后的帧

    //缓存区
    uint8_t  *out_buffer= (uint8_t *)av_malloc(avpicture_get_size(AV_PIX_FMT_RGBA,
                                                                  mAvContext->width,mAvContext->height));
    //与缓存区相关联，设置rgb_frame缓存区
    avpicture_fill((AVPicture *)rgb_frame,out_buffer,AV_PIX_FMT_RGBA,mAvContext->width,mAvContext->height);
    SwsContext* swsContext = sws_getContext(mAvContext->width,mAvContext->height,mAvContext->pix_fmt,
                                            mAvContext->width,mAvContext->height,AV_PIX_FMT_RGBA,
                                            SWS_BICUBIC,NULL,NULL,NULL);
    //取到nativewindow
    ANativeWindow *nativeWindow=ANativeWindow_fromSurface(env,surface);
    if(nativeWindow==0){
        LOGE("nativewindow取到失败")
        return;
    }
    //视频缓冲区
    ANativeWindow_Buffer native_outBuffer;

    while(1) {
        int ret = av_read_frame(avFormatContext, packet);
        if (ret != 0) {
            av_strerror(ret, buf, sizeof(buf));
            LOGE("--%s--\n", buf);
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
            while (result >= 0) {
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

                LOGE("转换并绘制")
                //绘制之前配置nativewindow
                ANativeWindow_setBuffersGeometry(nativeWindow, mAvContext->width,
                                                 mAvContext->height, WINDOW_FORMAT_RGBA_8888);
                //上锁
                ANativeWindow_lock(nativeWindow, &native_outBuffer, NULL);
                //转换为rgb格式
                sws_scale(swsContext, (const uint8_t *const *) frame->data, frame->linesize, 0,
                          frame->height, rgb_frame->data,
                          rgb_frame->linesize);
                //  rgb_frame是有画面数据
                uint8_t *dst = (uint8_t *) native_outBuffer.bits;
                //拿到一行有多少个字节 RGBA
                int destStride = native_outBuffer.stride * 4;
                //像素数据的首地址
                uint8_t *src = rgb_frame->data[0];
                //实际内存一行数量
                int srcStride = rgb_frame->linesize[0];
                //int i=0;
                for (int i = 0; i < mAvContext->height; ++i) {
                    //将rgb_frame中每一行的数据复制给nativewindow
                    memcpy(dst + i * destStride, src + i * srcStride, srcStride);
                }
                //解锁
                ANativeWindow_unlockAndPost(nativeWindow);
                av_frame_unref(frame);
            }
        }
    }


    //释放
    ANativeWindow_release(nativeWindow);
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    avcodec_close(mAvContext);
    avformat_free_context(avFormatContext);
#if ABSFILTER_ENABLE
    av_bsf_free(&absCtx);
    absCtx = NULL;
#endif
    env->ReleaseStringUTFChars(input_str, inputPath);
}