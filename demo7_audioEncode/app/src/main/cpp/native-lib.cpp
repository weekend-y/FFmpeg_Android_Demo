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
#include <libavutil/opt.h>
#ifdef __cplusplus
}
#endif

#define LOGE(FORMAT, ...) __android_log_print(ANDROID_LOG_ERROR,"Weekend",FORMAT,##__VA_ARGS__);

int audio_flush_encoder(AVFormatContext *fmt_ctx,unsigned int stream_index){
    int ret;
    int got_frame;
    AVPacket enc_pkt;
    if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities &
          AV_CODEC_CAP_DELAY))
        return 0;
    while (1) {
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_audio2 (fmt_ctx->streams[stream_index]->codec, &enc_pkt,
                                     NULL, &got_frame);
        av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame){
            ret=0;
            break;
        }
        LOGE("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n",enc_pkt.size);
        /* mux encoded frame */
        ret = av_write_frame(fmt_ctx, &enc_pkt);
        if (ret < 0)
            break;
    }
    return ret;
}

bool audio_encode(JNIEnv *env, jstring filename,jstring output)
{
    AVFormatContext* pFormatCtx;
    AVOutputFormat* fmt;
    AVStream* audio_st;
    AVCodecContext* pCodecCtx;
    AVCodec* pCodec;

    const char *inputPath = env->GetStringUTFChars(filename, JNI_FALSE);
    const char *outputPath = env->GetStringUTFChars(output, JNI_FALSE);

    uint8_t* frame_buf;
    AVFrame* pFrame;
    AVPacket *pkt;
    SwrContext *swr;

    int i;

    int ret=0;
    int size=0;

    FILE *in_file=NULL;	                        //Raw PCM data
    in_file= fopen(inputPath, "rb");

    int framenum=10000;                          //Audio frame number

    av_register_all();

    //方式1
    pFormatCtx = avformat_alloc_context();
    fmt = av_guess_format(NULL, outputPath, NULL);
    pFormatCtx->oformat = fmt;

    //方式2
    //avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, out_file);
    //fmt = pFormatCtx->oformat;

    //Open output
    if (avio_open(&pFormatCtx->pb,outputPath, AVIO_FLAG_READ_WRITE) < 0){
        LOGE("Failed to open output file!");
        return false;
    }

    audio_st = avformat_new_stream(pFormatCtx, 0);
    if (audio_st==NULL){
        return false;
    }

    //{ 这个参数的设定是输入的PCM是44100，FLT，STEREO，输出AAC是44100，FLTP，STEREO
    pCodecCtx = audio_st->codec;
    pCodecCtx->codec_id = fmt->audio_codec;
    pCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    pCodecCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    pCodecCtx->sample_rate= 44100;
    pCodecCtx->channel_layout=AV_CH_LAYOUT_STEREO;
    pCodecCtx->channels = av_get_channel_layout_nb_channels(pCodecCtx->channel_layout);
    pCodecCtx->bit_rate = 64000;
    pCodecCtx->profile=FF_PROFILE_AAC_MAIN;
    pCodecCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
    //Show some information
    av_dump_format(pFormatCtx, 0, outputPath, 1);

    swr = swr_alloc();
    av_opt_set_int(swr, "in_channel_layout",  AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO,  0);
    av_opt_set_int(swr, "in_sample_rate",     44100, 0);
    av_opt_set_int(swr, "out_sample_rate",    44100, 0);
    av_opt_set_sample_fmt(swr, "in_sample_fmt",  AV_SAMPLE_FMT_FLT, 0);
    av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLTP,  0);
    swr_init(swr);
    //}

    pCodec = avcodec_find_encoder(fmt->audio_codec);
    if (!pCodec){
        LOGE("Can not find encoder!");
        return false;
    }

    uint8_t *outs[2];
    int len = 4096;

    outs[0]=(uint8_t *)malloc(len);//len 为4096
    outs[1]=(uint8_t *)malloc(len);

    ret = avcodec_open2(pCodecCtx, pCodec,NULL);
    if (ret < 0){
        LOGE("Failed to open encoder!");
        return false;
    }

    pFrame = av_frame_alloc();
    pFrame->nb_samples= pCodecCtx->frame_size;
    pFrame->format=  pCodec->sample_fmts[0];

    size = av_samples_get_buffer_size(NULL, pCodecCtx->channels,pCodecCtx->frame_size,pCodecCtx->sample_fmt, 1);
    LOGE("size = %d , pCodecCtx->frame_size = %d,pframe->nb_samples = %d",size,pCodecCtx->frame_size,pFrame->nb_samples);
    frame_buf = (uint8_t *)av_malloc(size);
    avcodec_fill_audio_frame(pFrame, pCodecCtx->channels, pCodecCtx->sample_fmt,(const uint8_t*)frame_buf, size, 1);

    //Write Header
    avformat_write_header(pFormatCtx,NULL);

    //av_new_packet(&pkt,size);
    pkt = av_packet_alloc();
    if (!pkt)
        return false;

    for (i=0; i<framenum; i++){
        //Read PCM data
        if ((ret = fread(frame_buf, 1, size, in_file)) <= 0){
            LOGE("fread pcm raw data failed\n");
            getc(in_file);
            if(feof(in_file)) {
                LOGE(" -> Because this is the file feof!");
                break;
            }
            return false;
        }

        int count=swr_convert(swr, outs,len*4,(const uint8_t **)&frame_buf,len/4);//len 为4096
        pFrame->data[0] =(uint8_t*)outs[0];//audioFrame 是VFrame
        pFrame->data[1] =(uint8_t*)outs[1];

        //pFrame->data[0] = frame_buf;  //PCM Data
        pFrame->pts=i*100;

        ret = avcodec_send_frame(pCodecCtx, pFrame);
        if (ret < 0){
            LOGE("Error sending a frame for encoding");
            return false;
        }

        while (ret >= 0){
            ret = avcodec_receive_packet(pCodecCtx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                break;
            }
            else if (ret < 0){
                LOGE("Error during encoding\n");
                break;
            }

            ret = av_write_frame(pFormatCtx, pkt);
            av_packet_unref(pkt);
        }
    }

    //Flush Encoder
    ret = audio_flush_encoder(pFormatCtx,0);
    if (ret < 0) {
        LOGE("Flushing encoder failed");
        return false;
    }

    //Write Trailer
    av_write_trailer(pFormatCtx);

    //Clean
    if (audio_st){
        avcodec_close(audio_st->codec);
        av_free(pFrame);
        av_free(frame_buf);
    }
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);

    fclose(in_file);

    LOGE("---- audio encode finished ----");

    return true;
}

extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpeg_1audioencode_MainActivity_encode_1audio(JNIEnv *env, jobject thiz,
                                                                jstring input_str,
                                                                jstring output_str) {
    LOGE("begin to audio encode");
    audio_encode(env,input_str,output_str);
}