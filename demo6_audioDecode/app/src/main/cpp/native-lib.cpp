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
#ifdef __cplusplus
}
#endif

//过滤器宏开关
#define ABSFILTER_ENABLE    1

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"Weekend",FORMAT,##__VA_ARGS__);

bool audio_decode(JNIEnv *env, jstring filename,jstring output)
{
    const char *inputPath = env->GetStringUTFChars(filename, JNI_FALSE);
    const char *outputPath = env->GetStringUTFChars(output, JNI_FALSE);

    FILE* fp_PCM = fopen(outputPath, "wb+");
    char buf[256];
    int i, ch, data_size;

    if (!fp_PCM){
        LOGE("open out.pcm failed");
        return false;
    }

    //注册各大组件
    av_register_all();
    LOGE("注册成功")
    AVFormatContext *avFormatContext = avformat_alloc_context();//获取上下文
    int error;
    //打开音频地址并获取里面的内容(解封装)
    error = avformat_open_input(&avFormatContext, inputPath, NULL, NULL);
    if (error < 0){
        LOGE("打开音频文件失败\n");
        return false;
    }
    if (avformat_find_stream_info(avFormatContext, NULL) < 0){
        LOGE("获取内容失败")
        return false;
    }

    //show
    av_dump_format(avFormatContext, 0, inputPath, 0);

    //获取音频的编码信息
    AVCodecParameters *origin_par = NULL;
    int mAudioStreamIdx = -1;
    mAudioStreamIdx = av_find_best_stream(avFormatContext, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (mAudioStreamIdx < 0) {
        av_log(NULL, AV_LOG_ERROR, "Can't find audio stream in input file\n");
        return false;
    }
    LOGE("成功找到音频流")
    origin_par = avFormatContext->streams[mAudioStreamIdx]->codecpar;

    // 寻找解码器 {start
    AVCodec *mAcodec = NULL;
    AVCodecContext *mAvContext = NULL;
    mAcodec = avcodec_find_decoder(origin_par->codec_id);
    mAvContext = avcodec_alloc_context3(mAcodec);
    if (!mAcodec || !mAvContext){
        return false;
    }

#if ABSFILTER_ENABLE
    //过滤器相关配置，这个与音频码流格式相关
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

                // 写文件保存音频数据
                data_size = av_get_bytes_per_sample(mAvContext->sample_fmt);
                if (data_size < 0) {
                    /* This should not occur, checking just for paranoia */
                    LOGE("Failed to calculate data size\n");
                    return false;
                }
                for (i = 0; i < frame->nb_samples; i++)
                    for (ch = 0; ch < mAvContext->channels; ch++)
                        fwrite(frame->data[ch] + data_size*i, 1, data_size, fp_PCM);

                av_frame_unref(frame);
            }
        }

    }

    fclose(fp_PCM);
    av_frame_free(&frame);
    avcodec_close(mAvContext);
    avformat_free_context(avFormatContext);
#if ABSFILTER_ENABLE
    av_bsf_free(&absCtx);
    absCtx = NULL;
#endif

    LOGE("--- audio decode finished ---");
    return true;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpeg_1audiodecode_MainActivity_decode_1audio(JNIEnv *env, jobject thiz,
                                                                jstring input_str,
                                                                jstring output_str) {
    LOGE("begin to audio decode");
    audio_decode(env,input_str,output_str);
}