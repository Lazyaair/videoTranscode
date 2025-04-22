#pragma once
#include <stdio.h>
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}
#include"queue.h"
#include"decoder.h"
#include"encoder.h"
#include"muxer.h"

// 定义媒体类型枚举
enum MediaType {
    MEDIA_TYPE_VIDEO,
    MEDIA_TYPE_AUDIO
};

typedef struct Demuxer
{
    AVFormatContext *fmt_ctx;
    int stream_index;   // 视频流索引
    int stream_index_a; // 音频流索引
    AVPacket *pkt;
    // FILE *output_file;

    // 视频参数
    int width;
    int height;
    enum AVPixelFormat pix_fmt;
    
    // 音频参数
    int sample_rate;
    int channels;
    enum AVSampleFormat sample_fmt;
}Demuxer;
int init_all(const char *input_filename, const char *output_filename,MediaType type,Demuxer* ctx_demuxer,Decoder* ctx_decoder,Encoder* ctx_encoder,Decoder* ctx_decoder_a,Encoder* ctx_encoder_a);
void cleanup_decoder(Demuxer* ctx_demuxer);
void print_ffplay_command(const char *output1_filename,const char *output2_filename, MediaType type, Demuxer* ctx_demuxer) ;

// void extract_video_(Demuxer *ctx_demuxer,const char *input_filename, const char *output_filename,PacketQueue &vinq);
void ext_video(Demuxer *ctx_demuxer,const char *input_filename,const char *output1_filename,const char *output2_filename,PacketQueue &vinq,PacketQueue &ainq);

//     AVFormatContext* fmt_ctx_;
//     int video_stream_index_;
//     int audio_stream_index_;
//     bool is_opened_;
// };