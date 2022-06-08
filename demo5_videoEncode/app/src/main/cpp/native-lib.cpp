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


int flush_encoder(AVFormatContext *fmt_ctx, unsigned int stream_index)
{
    int ret;
    int got_frame;
    AVPacket enc_pkt;
    if (!(fmt_ctx->streams[stream_index]->codec->codec->capabilities & AV_CODEC_CAP_DELAY)){
        return 0;
    }

    while (1){
        enc_pkt.data = NULL;
        enc_pkt.size = 0;
        av_init_packet(&enc_pkt);
        ret = avcodec_encode_video2(fmt_ctx->streams[stream_index]->codec, &enc_pkt,
                                    NULL, &got_frame);

        av_frame_free(NULL);
        if (ret < 0)
            break;
        if (!got_frame) {
            ret = 0;
            break;
        }
        LOGE("Flush Encoder: Succeed to encode 1 frame!\tsize:%5d\n", enc_pkt.size);
        /* mux encoded frame */
        ret = av_write_frame(fmt_ctx, &enc_pkt);
        if (ret < 0)
            break;
    }
    return ret;
}

bool video_encode(JNIEnv *env, jstring filename,jstring output)
{
    AVFormatContext* pFormatCtx;
    AVOutputFormat* fmt;
    AVStream* video_st;
    AVCodecContext* pCodecCtx;
    AVCodec* pCodec;
    AVPacket *pkt;
    uint8_t* picture_buf;
    AVFrame* pFrame;
    const char *inputPath = env->GetStringUTFChars(filename, JNI_FALSE);
    const char *outputPath = env->GetStringUTFChars(output, JNI_FALSE);
    int picture_size;
    int yuv_420_size;
    int framecnt = 0;
    int ret;

    FILE *in_file = fopen(inputPath, "rb");   //Input raw YUV data
    if(!in_file){
        LOGE(" fopen faile");
        return false;
    }
    int in_w = 448, in_h = 960;                 	//Input data's width and height
    int framenum = 10000;                        	//Frames to encode

    av_register_all();
    //方式1
    pFormatCtx = avformat_alloc_context();
    //Guess Format
    fmt = av_guess_format(NULL, outputPath, NULL);
    pFormatCtx->oformat = fmt;

    //方式2
    //avformat_alloc_output_context2(&pFormatCtx, NULL, NULL, out_file);
    //fmt = pFormatCtx->oformat;

    //Open output
    if (avio_open(&pFormatCtx->pb, outputPath, AVIO_FLAG_READ_WRITE) < 0) {
        LOGE("Failed to open output file! \n");
        return false;
    }

    video_st = avformat_new_stream(pFormatCtx, 0);
    //video_st->time_base.num = 1;
    //video_st->time_base.den = 25;

    if (video_st == NULL) {
        return false;
    }

    //Param that must set
    pCodecCtx = video_st->codec;
    //pCodecCtx->codec_id =AV_CODEC_ID_HEVC;
    pCodecCtx->codec_id = fmt->video_codec;
    pCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
    pCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    pCodecCtx->width = in_w;
    pCodecCtx->height = in_h;
    pCodecCtx->bit_rate = 400000;
    pCodecCtx->gop_size = 25;   //I帧间隔

    pCodecCtx->time_base.num = 1;
    pCodecCtx->time_base.den = 25;  //time_base一般是帧率的倒数，但不总是
    pCodecCtx->framerate.num = 25;  //帧率
    pCodecCtx->framerate.den = 1;


    AVCodecParameters *codecpar = video_st->codecpar;
    codecpar->codec_id = fmt->video_codec;
    codecpar->width = in_w;
    codecpar->height = in_h;
    codecpar->bit_rate = 400000;
    codecpar->format = AV_PIX_FMT_YUV420P;

    ///AVFormatContext* mFormatCtx
    ///mBitRate   = mFormatCtx->bit_rate;   ///码率存储位置
    ///mFrameRate = mFormatCtx->streams[stream_id]->avg_frame_rate.num;


    //H264
    //pCodecCtx->me_range = 16;
    //pCodecCtx->max_qdiff = 4;
    //pCodecCtx->qcompress = 0.6;
    pCodecCtx->qmin = 1;
    pCodecCtx->qmax = 20;

    //Optional Param
    pCodecCtx->max_b_frames = 0;  //不要B帧

    // Set Option
    AVDictionary *param = 0;
    //H.264
    if (pCodecCtx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "preset", "slow", 0);
        //av_dict_set(&param, "tune", "zerolatency", 0);
        //av_dict_set(&param, "profile", "main", 0);
    }
    //H.265
    if (pCodecCtx->codec_id == AV_CODEC_ID_H265) {
        av_dict_set(&param, "preset", "ultrafast", 0);
        av_dict_set(&param, "tune", "zero-latency", 0);
    }

    //Show some Information
    av_dump_format(pFormatCtx, 0, outputPath, 1);

    pCodec = avcodec_find_encoder(pCodecCtx->codec_id);
    if (!pCodec) {
        LOGE("Can not find encoder!");
        return false;
    }
    if (avcodec_open2(pCodecCtx, pCodec, &param) < 0) {
        LOGE("Failed to open encoder!");
        return false;
    }

    pFrame = av_frame_alloc();
    pFrame->format = AV_PIX_FMT_YUV420P;
    pFrame->width  = in_w;
    pFrame->height = in_h;

    picture_size = av_image_get_buffer_size(pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 1);
    picture_buf = (uint8_t *)av_malloc(picture_size);
    av_image_fill_arrays(pFrame->data, pFrame->linesize, picture_buf, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height, 1);

    //写文件头（对于某些没有文件头的封装格式，不需要此函数。比如说MPEG2TS）。
    avformat_write_header(pFormatCtx, NULL);

    //av_new_packet(pkt, picture_size);
    pkt = av_packet_alloc();
    if (!pkt)
        return false;

    yuv_420_size = pCodecCtx->width * pCodecCtx->height * 3 /2;
    int y_size   = pCodecCtx->width * pCodecCtx->height;

    LOGE("yuv_420_size = %d\n",yuv_420_size);

    for (int i = 0; i < framenum; i++){
        int ret = 0;
        //Read raw YUV data
        if ((ret = fread(picture_buf, 1, yuv_420_size, in_file)) <= 0){
            LOGE("fread raw data failed\n");
            getc(in_file);
            if(feof(in_file)) {
                LOGE(" -> Because this is the file feof! \n");
                break;
            }
            return false;
        }

        pFrame->data[0] = picture_buf;              // Y
        pFrame->data[1] = picture_buf + y_size;      // U
        pFrame->data[2] = picture_buf + y_size * 5 / 4;  // V
        //PTS
        pFrame->pts = i;

        ret = avcodec_send_frame(pCodecCtx, pFrame);
        if (ret < 0){
            LOGE("Error sending a frame for encoding\n");
            return false;
        }

        while (ret >= 0)
        {
            ret = avcodec_receive_packet(pCodecCtx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                break;
            }
            else if (ret < 0){
                LOGE("Error during encoding\n");
                break;
            }

            framecnt++;
            ret = av_write_frame(pFormatCtx, pkt);
            av_packet_unref(pkt);
        }

    }

    //Flush Encoder
    // 输入的像素数据读取完成后调用此函数，用于输出编码器中剩余的AVPacket
    ret = flush_encoder(pFormatCtx, 0);
    if (ret < 0) {
        LOGE("Flushing encoder failed\n");
        return false;
    }

    //Write file trailer
    av_write_trailer(pFormatCtx);

    //Clean
    if (video_st) {
        avcodec_close(video_st->codec);
        av_free(pFrame);
        av_free(picture_buf);
    }
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);
    av_packet_free(&pkt);

    fclose(in_file);

    LOGE("--- video encode finished ---");

    return true;
}


extern "C"
JNIEXPORT void JNICALL
Java_com_example_ffmpeg_1videoencode_MainActivity_encode_1test(JNIEnv *env, jobject thiz,
                                                               jstring input_str, jint flag,
                                                               jstring output_str) {
    LOGE("begin to video encode");
    video_encode(env,input_str,output_str);
}