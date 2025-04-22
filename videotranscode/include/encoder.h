#pragma once
#include <stdio.h>
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}
#include"soundtouch/SoundTouch.h"
#include"queue.h"

// 定义编码器结构体
typedef struct Encoder {
    AVCodecContext *enc_ctx;
    const AVCodec *codec;
    AVFrame *frame;
    int64_t next_pts;
    
    // 编码参数
    int width;
    int height;
    enum AVPixelFormat pix_fmt;
    int fps;
    int bitrate;
    
    // 音频参数
    int sample_rate;
    int channels;
    enum AVSampleFormat sample_fmt;
    int audio_bitrate;
} Encoder;

// 初始化视频编码器
int encoder_init_video(Encoder *encoder, int width, int height, 
                      enum AVPixelFormat pix_fmt, int fps, int bitrate);

// 初始化音频编码器
int encoder_init_audio(Encoder *encoder, int sample_rate, int channels,
                      enum AVSampleFormat sample_fmt, int bitrate);

// 编码一帧视频
int encoder_encode_video(Encoder *encoder, AVFrame *frame, AVPacket *pkt);

// 编码一帧音频
int encoder_encode_audio(Encoder *encoder, AVFrame *frame, AVPacket *pkt);

// 冲洗编码器
int encoder_flush(Encoder *encoder, AVPacket *pkt);

// 清理资源
void encoder_cleanup(Encoder *encoder);

// 线程封装
void encode_thread(Encoder *encoder, FrametQueue& frame_queue, PacketQueue& packet_queue,float speed);

//音频线程封装
void encode_audio_thread(Encoder *encoder, FrametQueue& frame_queue, PacketQueue& packet_queue,float speed);