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
#include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

//过滤器宏开关
#define ABSFILTER_ENABLE    1

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"Weekend",FORMAT,##__VA_ARGS__);


extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpeg_1byaudiotrack_MusicPlay_playSound(JNIEnv *env, jobject thiz,
                                                          jstring input) {

    const char *inputPath = env->GetStringUTFChars(input, 0);
    char buf[256];

    //注册各大组件
    av_register_all();
    LOGE("注册成功")
    AVFormatContext *avFormatContext = avformat_alloc_context();//获取上下文
    int error;
    //打开音频地址并获取里面的内容(解封装)
    error = avformat_open_input(&avFormatContext, inputPath, NULL, NULL);
    if (error < 0){
        LOGE("打开音频文件失败\n");
        return;
    }
    if (avformat_find_stream_info(avFormatContext, NULL) < 0){
        LOGE("获取内容失败")
        return;
    }

    //show
    av_dump_format(avFormatContext, 0, inputPath, 0);

    //获取音频的编码信息
    int i=0;
    int mAudioStreamIdx = -1;
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            LOGE("  找到音频id %d", avFormatContext->streams[i]->codec->codec_type);
            mAudioStreamIdx=i;
            break;
        }
    }

    // 寻找解码器 {start
    //获取解码器上下文
    AVCodecContext *mAvContext=avFormatContext->streams[mAudioStreamIdx]->codec;
    //获取解码器
    AVCodec *mAcodec = NULL;
    mAcodec = avcodec_find_decoder(mAvContext->codec_id);

#if ABSFILTER_ENABLE
    //过滤器相关配置，这个与音频码流格式相关，也可以不用
    const AVBitStreamFilter * absFilter = NULL;
    AVBSFContext *absCtx = NULL;
    AVCodecParameters *codecpar = NULL;
    absFilter = av_bsf_get_by_name("mp3decomp");
    //过滤器分配内存
    av_bsf_alloc(absFilter, &absCtx);
    //添加解码器属性
    codecpar = avFormatContext->streams[mAudioStreamIdx]->codecpar;
    avcodec_parameters_copy(absCtx->par_in, codecpar);
    absCtx->time_base_in = avFormatContext->streams[mAudioStreamIdx]->time_base;
    //初始化过滤器上下文
    av_bsf_init(absCtx);
#endif

    // 打开解码器
    if (avcodec_open2(mAvContext, mAcodec, NULL) != 0){
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

    //得到SwrContext ，进行重采样 {start
    SwrContext *swrContext = swr_alloc();
    //缓存区
    uint8_t *out_buffer = (uint8_t *) av_malloc(44100 * 2);
    //输出的声道布局（立体声）
    uint64_t  out_ch_layout=AV_CH_LAYOUT_STEREO;
    //输出采样位数  16位
    enum AVSampleFormat out_formart=AV_SAMPLE_FMT_S16;
    //输出的采样率必须与输入相同
    int out_sample_rate = mAvContext->sample_rate;
    //swr_alloc_set_opts将PCM源文件的采样格式转换为自己希望的采样格式
    swr_alloc_set_opts(swrContext, out_ch_layout, out_formart, out_sample_rate,
                       mAvContext->channel_layout, mAvContext->sample_fmt, mAvContext->sample_rate, 0,
                       NULL);
    swr_init(swrContext);
    LOGE("设置重采样成功")
    //end}

    //获取通道数  2
    int out_channer_nb = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    //反射得到Class类型
    jclass david_player = env->GetObjectClass(thiz);
    //反射得到createAudio方法
    jmethodID createAudio = env->GetMethodID(david_player, "createTrack", "(II)V");
    //反射调用createAudio
    env->CallVoidMethod(thiz, createAudio, 44100, out_channer_nb);
    //反射得到playTrack方法
    jmethodID audio_write = env->GetMethodID(david_player, "playTrack", "([BI)V");

    while(1)
    {
        int ret = av_read_frame(avFormatContext, packet);
        if (ret != 0){
            av_strerror(ret,buf,sizeof(buf));
            LOGE("--%s--\n",buf);
            av_packet_unref(packet);
            break;
        }


        if (ret >= 0 && packet->stream_index != mAudioStreamIdx){
            av_packet_unref(packet);
            continue;
        }

#if ABSFILTER_ENABLE
        if (av_bsf_send_packet(absCtx, packet) < 0){
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
            if (result < 0){
                av_log(NULL, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
                continue;
            }

            // 接收解码数据
            while (result >= 0){
                result = avcodec_receive_frame(mAvContext, frame);
                if (result == AVERROR_EOF)
                    break;
                else if (result == AVERROR(EAGAIN)){
                    result = 0;
                    break;
                }
                else if (result < 0){
                    av_log(NULL, AV_LOG_ERROR, "Error decoding frame\n");
                    av_frame_unref(frame);
                    break;
                }

                LOGE("解码播放")
                swr_convert(swrContext, &out_buffer, 44100 * 2, (const uint8_t **) frame->data, frame->nb_samples);
                //缓冲区的大小
                int size = av_samples_get_buffer_size(NULL, out_channer_nb, frame->nb_samples,
                                                      AV_SAMPLE_FMT_S16, 1);
                jbyteArray audio_sample_array = env->NewByteArray(size);
                env->SetByteArrayRegion(audio_sample_array, 0, size, (const jbyte *) out_buffer);
                env->CallVoidMethod(thiz, audio_write, audio_sample_array, size);
                env->DeleteLocalRef(audio_sample_array);

                av_frame_unref(frame);
            }
        }

    }

    swr_free(&swrContext);
    av_frame_free(&frame);
    avcodec_close(mAvContext);
    avformat_free_context(avFormatContext);
#if ABSFILTER_ENABLE
    av_bsf_free(&absCtx);
    absCtx = NULL;
#endif
    env->ReleaseStringUTFChars(input, inputPath);
}