#pragma once
#include <stdio.h>
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
}
#include"queue.h"
#include"encoder.h"

// 定义封装器结构体
typedef struct Muxer {
    AVFormatContext *fmt_ctx;
    AVStream *video_stream;
    AVStream *audio_stream;
    const char *filename;
    int have_video;
    int have_audio;
    
    // 视频参数
    int width;
    int height;
    enum AVPixelFormat pix_fmt;
    int fps;
    int64_t next_pts;
    
    // 音频参数
    int sample_rate;
    int channels;
    enum AVSampleFormat sample_fmt;
    int64_t audio_next_pts;
} Muxer;

// 初始化封装器
int muxer_init(Muxer *muxer, const char *filename);

// 添加视频流
int muxer_add_video_stream(Muxer *muxer, int width, int height, 
                          enum AVPixelFormat pix_fmt, int fps);

// 添加音频流
int muxer_add_audio_stream(Muxer *muxer, int sample_rate, int channels,
                          enum AVSampleFormat sample_fmt);

// 写入数据包
int muxer_write_packet(Muxer *muxer, AVPacket *pkt);

// 写入文件头
int muxer_write_header(Muxer *muxer);

// 写入文件尾
int muxer_write_trailer(Muxer *muxer);

// 清理资源
void muxer_cleanup(Muxer *muxer);
void muxer_thread( const char *output_filename,Encoder* ctx_encoder, Encoder* ctx_encoder_a, PacketQueue& v_out_q, PacketQueue& a_out_q, Muxer* muxer);