//
// Created by weiqingyang on 2022/6/10.
//

#include "FFmpegMusic.h"

AVFormatContext *avFormatContext;
AVCodecContext *mAvContext;
AVCodec *mAcodec;
AVPacket *packet;
AVFrame *frame;
SwrContext *swrContext;
uint8_t *out_buffer;
int out_channer_nb;
int mAudioStreamIdx = -1;

//过滤器宏开关
#define ABSFILTER_ENABLE    1

#if ABSFILTER_ENABLE
const AVBitStreamFilter * absFilter = NULL;
AVBSFContext *absCtx = NULL;
AVCodecParameters *codecpar = NULL;
#endif

//opensl es调用 int * rate,int *channel
int createFFmpeg(JNIEnv *env,int *rate,int *channel,jstring filename){

    //注册各大组件
    av_register_all();
    LOGE("注册成功")

    const char *inputPath = env->GetStringUTFChars(filename, JNI_FALSE);
    //char *input = "/sdcard/input.mp3";

    avFormatContext = avformat_alloc_context();
    LOGE("--> %s",inputPath);
    LOGE("xxx %p",avFormatContext);

    int error;
    char buf[] = "";
    //打开音频地址并获取里面的内容(解封装)
    error = avformat_open_input(&avFormatContext, inputPath, NULL, NULL);
    if (error != 0) {
        av_strerror(error, buf, 1024);
        LOGE("Couldn't open file %s: %d(%s)", inputPath, error, buf);
        LOGE("打开音频文件失败")
    }
    if(avformat_find_stream_info(avFormatContext,NULL) < 0){
        LOGE("获取内容失败")
        return -1;
    }

    //show
    av_dump_format(avFormatContext, 0, inputPath, 0);

    //获取音频的编码信息
    int i=0;
    for (int i = 0; i < avFormatContext->nb_streams; ++i) {
        if (avFormatContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            LOGE("  找到音频id %d", avFormatContext->streams[i]->codec->codec_type);
            mAudioStreamIdx=i;
            break;
        }
    }

    // 寻找解码器 {start
    //获取解码器上下文
    mAvContext=avFormatContext->streams[mAudioStreamIdx]->codec;
    //获取解码器
    mAcodec = avcodec_find_decoder(mAvContext->codec_id);

#if ABSFILTER_ENABLE
    //过滤器相关配置，这个与音频码流格式相关，也可以不用
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
        return -1;
    }
    LOGE("解码器打开成功")
    // 寻找解码器 end}

    //申请AVPacket
    packet = (AVPacket *) av_malloc(sizeof(AVPacket));
    av_init_packet(packet);
    //申请AVFrame
    frame = av_frame_alloc();//分配一个AVFrame结构体,AVFrame结构体一般用于存储原始数据，指向解码后的原始帧

    //得到SwrContext ，进行重采样 {start
    swrContext = swr_alloc();
    //缓存区
    out_buffer = (uint8_t *) av_malloc(44100 * 2);
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
    out_channer_nb = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    *rate = mAvContext->sample_rate;
    *channel = mAvContext->channels;
    return 0;
}


int getPcm(void **pcm,size_t *pcm_size){
    char buf[256];
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
                *pcm = out_buffer;
                *pcm_size = size;

                av_frame_unref(frame);
                return 0;
            }
        }

    }

    return 0;
}


void realseFFmpeg(){
    av_free_packet(packet);
    av_free(out_buffer);
    av_frame_free(&frame);
    swr_free(&swrContext);
    avcodec_close(mAvContext);
    avformat_close_input(&avFormatContext);
}
