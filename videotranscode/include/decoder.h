#pragma once
extern "C" {
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
}
#include<thread>
#include<iostream>
#include"queue.h"
#include"filter.h"

// 解码相关的上下文
typedef struct Decoder {
    AVCodecContext *dec_ctx;
    AVCodec *codec;
    AVFrame *frame;
    FILE *output_file;
    Decoder():dec_ctx(nullptr),codec(nullptr),frame(nullptr){};
} Decoder;

int video_decode(const char *output_filename,Decoder* ctx_decoder,PacketQueue& vinq,FrametQueue& vfq);
int audio_decode(const char *output_filename,Decoder* ctx_decoder,PacketQueue& ainq,FrametQueue& afq);