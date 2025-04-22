#include <stdio.h>
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}
#include"demuxer.h"
#include"muxer.h"

void print_ffplay_command(const char *output1_filename,const char *output2_filename, MediaType type,Demuxer* ctx_demuxer){
    // 打印视频播放命令
    printf("\n=== 视频播放命令 ===\n");
    printf("未旋转ffplay -f rawvideo -pixel_format yuv420p -video_size %dx%d %s\n\n",
        ctx_demuxer->width, ctx_demuxer->height, output1_filename);
    printf("旋转后ffplay -f rawvideo -pixel_format yuv420p -video_size %dx%d %s\n\n",
        ctx_demuxer->height, ctx_demuxer->width, output1_filename);

    // 打印音频播放命令
    const char *fmt_name;
    switch (ctx_demuxer->sample_fmt) {
        case AV_SAMPLE_FMT_S16:
            fmt_name = "s16le";
            break;
        case AV_SAMPLE_FMT_FLT:
            fmt_name = "f32le";
            break;
        case AV_SAMPLE_FMT_S32:
            fmt_name = "s32le";
            break;
        default:
            fmt_name = "s16le"; // 默认格式
    }
    
    printf("\n=== 音频播放命令 ===\n");
    printf("ffplay -f %s -ar %d -ac %d %s\n\n",
           fmt_name, ctx_demuxer->sample_rate, ctx_demuxer->channels, output2_filename);
}

// 通用的初始化函数
int init_all(const char *input_filename,const char *output_filename, MediaType type,Demuxer* ctx_demuxer,Decoder* ctx_decoder,Encoder* ctx_encoder,Decoder* ctx_decoder_a,Encoder* ctx_encoder_a) {
    // 初始化结构体
    ctx_demuxer->fmt_ctx = NULL;
    // CTX_DEMUXER.dec_ctx = NULL;
    // CTX_DEMUXER.codec = NULL;
    ctx_demuxer->stream_index = -1;
    ctx_demuxer->stream_index_a=-1;
    ctx_demuxer->pkt = av_packet_alloc();
    // CTX_DEMUXER->frame = av_frame_alloc();
    if (!ctx_demuxer->pkt) {
        printf("无法分配数据包\n");
        return -1;
    }

    // 分配视频和音频解码帧
    ctx_decoder->frame = av_frame_alloc();
    ctx_decoder_a->frame = av_frame_alloc();
    if (!ctx_decoder->frame) {
        printf("无法分配解码帧\n");
        return -1;
    }


    // 打开输入文件
    if (avformat_open_input(&ctx_demuxer->fmt_ctx, input_filename, NULL, NULL) < 0) {
        printf("无法打开输入文件\n");
        return -1;
    }

    // 获取流信息
    if (avformat_find_stream_info(ctx_demuxer->fmt_ctx, NULL) < 0) {
        printf("无法获取流信息\n");
        return -1;
    }

    // 查找对应的流
    AVMediaType target_type = (type == MEDIA_TYPE_VIDEO) ? 
                             AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO;
    
    for (int i = 0; i < ctx_demuxer->fmt_ctx->nb_streams; i++) {
        AVCodecParameters *codecParams = ctx_demuxer->fmt_ctx->streams[i]->codecpar;
        // 找到视频流
        if (codecParams->codec_type == AVMEDIA_TYPE_VIDEO) {
            ctx_demuxer->stream_index = i;
            printf("查找视频解码器\n");
            // 查找解码器
            ctx_decoder->codec = avcodec_find_decoder(ctx_demuxer->fmt_ctx->streams[i]->codecpar->codec_id);
            if (!ctx_decoder->codec) {
                printf("无法找到解码器\n");
                return -1;
            }
            printf("分配视频解码器上下文\n");
            
            // 分配解码器上下文
            ctx_decoder->dec_ctx = avcodec_alloc_context3(ctx_decoder->codec);
            if (!ctx_decoder->dec_ctx) {
                printf("无法分配解码器上下文\n");
                return -1;
            }
            
            printf("复制视频编解码器参数\n");
            // 复制编解码器参数
            if (avcodec_parameters_to_context(ctx_decoder->dec_ctx, 
                ctx_demuxer->fmt_ctx->streams[i]->codecpar) < 0) {
                printf("无法复制编解码器参数\n");
                return -1;
            }
            
            printf("打开视频解码器\n");
            // 打开解码器
            if (avcodec_open2(ctx_decoder->dec_ctx, ctx_decoder->codec, NULL) < 0) {
                printf("无法打开解码器\n");
                return -1;
            }
            // 保存视频参数
            ctx_demuxer->width = ctx_decoder->dec_ctx->width;
            ctx_demuxer->height = ctx_decoder->dec_ctx->height;
            ctx_demuxer->pix_fmt = ctx_decoder->dec_ctx->pix_fmt;
        }
        else if (codecParams->codec_type == AVMEDIA_TYPE_AUDIO){   // 找到音频流
            ctx_demuxer->stream_index_a = i;
            printf("找到音频流，索引：%d\n", i);
            
            // 初始化音频解码器
            ctx_decoder_a->codec = avcodec_find_decoder(codecParams->codec_id);
            if (!ctx_decoder_a->codec) {
                printf("无法找到音频解码器\n");
                return -1;
            }
            
            ctx_decoder_a->dec_ctx = avcodec_alloc_context3(ctx_decoder_a->codec);
            if (!ctx_decoder_a->dec_ctx) {
                printf("无法分配音频解码器上下文\n");
                return -1;
            }
            
            if (avcodec_parameters_to_context(ctx_decoder_a->dec_ctx, codecParams) < 0) {
                printf("无法复制音频解码器参数\n");
                return -1;
            }
            
            if (avcodec_open2(ctx_decoder_a->dec_ctx, ctx_decoder_a->codec, NULL) < 0) {
                printf("无法打开音频解码器\n");
                return -1;
            }
            
            // 保存音频参数
            ctx_demuxer->sample_rate = ctx_decoder_a->dec_ctx->sample_rate;
            ctx_demuxer->channels = ctx_decoder_a->dec_ctx->channels;
            ctx_demuxer->sample_fmt = ctx_decoder_a->dec_ctx->sample_fmt;
        }
    }

    // 检查是否找到了视频和音频流
    if (ctx_demuxer->stream_index == -1) {
        printf("找不到视频流\n");
        return -1;
    }
    if (ctx_demuxer->stream_index_a == -1) {
        printf("找不到音频流\n");
        return -1;
    }

    // 获取输入视频流
    AVStream* input_stream = ctx_demuxer->fmt_ctx->streams[ctx_demuxer->stream_index];
    // 保存基本参数
    ctx_demuxer->width = ctx_decoder->dec_ctx->width;
    ctx_demuxer->height = ctx_decoder->dec_ctx->height;
    ctx_demuxer->pix_fmt = ctx_decoder->dec_ctx->pix_fmt;

    // 设置时基
    if (input_stream->time_base.num <= 0 || input_stream->time_base.den <= 0) {
        ctx_decoder->dec_ctx->time_base = (AVRational){1, 25};
        // printf("使用默认时基: 1/25\n");
    } else {
        ctx_decoder->dec_ctx->time_base = input_stream->time_base;
        // printf("使用输入流时基: %d/%d\n", 
        //         ctx_decoder->dec_ctx->time_base.num,
        //         ctx_decoder->dec_ctx->time_base.den);
    }

    // 设置像素宽高比
    if (input_stream->sample_aspect_ratio.num <= 0 || 
        input_stream->sample_aspect_ratio.den <= 0) {
        ctx_decoder->dec_ctx->sample_aspect_ratio = (AVRational){1, 1};
        // printf("使用默认像素宽高比: 1:1\n");
    } else {
        ctx_decoder->dec_ctx->sample_aspect_ratio = input_stream->sample_aspect_ratio;
        // printf("使用输入流像素宽高比: %d:%d\n",
        //         ctx_decoder->dec_ctx->sample_aspect_ratio.num,
        //         ctx_decoder->dec_ctx->sample_aspect_ratio.den);
    }
        
    // 计算帧率
    int fps = 25; // 默认帧率
    if (input_stream->avg_frame_rate.den && input_stream->avg_frame_rate.num) {
        fps = av_q2d(input_stream->avg_frame_rate);
        // printf("使用平均帧率: %d\n", fps);
    } else if (input_stream->r_frame_rate.den && input_stream->r_frame_rate.num) {
        fps = av_q2d(input_stream->r_frame_rate);
        // printf("使用实时帧率: %d\n", fps);
    } else {
        // printf("使用默认帧率: %d\n", fps);
    }

    // 验证并设置像素格式
    if (ctx_decoder->dec_ctx->pix_fmt == AV_PIX_FMT_NONE) {
        ctx_decoder->dec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        // printf("使用默认像素格式: YUV420P\n");
    }

    // 获取或设置码率
    int bitrate = ctx_decoder->dec_ctx->bit_rate;
    if (!bitrate) {
        // 如果解码器没有码率信息，使用一个合理的默认值
        // 这里使用一个基于分辨率的简单计算
        bitrate = (int)(ctx_decoder->dec_ctx->width * ctx_decoder->dec_ctx->height * fps * 0.1);
        // 确保码率在合理范围内
        bitrate = FFMAX(500000, FFMIN(bitrate, 10000000));
        // printf("使用计算的码率: %d bps\n", bitrate);
    } else {
        // printf("使用输入流码率: %d bps\n", bitrate);
    }

    // printf("视频编码器初始化参数:\n");
    // printf("分辨率: %dx%d\n", ctx_decoder->dec_ctx->width, ctx_decoder->dec_ctx->height);
    // printf("像素格式: %d\n", ctx_decoder->dec_ctx->pix_fmt);
    // printf("帧率: %d\n", fps);
    // printf("码率: %d bps\n", bitrate);

    // 初始化视频编码器
    if (encoder_init_video(ctx_encoder,
                            ctx_decoder->dec_ctx->height,    // 宽度 旋转90度
                            ctx_decoder->dec_ctx->width,   // 高度
                            ctx_decoder->dec_ctx->pix_fmt,  // 像素格式
                            fps,                            // 帧率
                            bitrate) < 0) {                 // 码率
        printf("无法初始化视频编码器\n");
        return -1;
    }
    printf("视频编码器初始化成功\n");

    //    
    // 获取或设置音频码率
    int audio_bitrate = ctx_decoder_a->dec_ctx->bit_rate;
    if (!audio_bitrate) {
        audio_bitrate = 128000; // 默认128kbps
        printf("使用默认音频码率: %d bps\n", audio_bitrate);
    } else {
        printf("使用输入流音频码率: %d bps\n", audio_bitrate);
    }

    // 初始化音频编码器
    if (encoder_init_audio(ctx_encoder_a,
        ctx_decoder_a->dec_ctx->sample_rate,
        ctx_decoder_a->dec_ctx->channels,
        ctx_decoder_a->dec_ctx->sample_fmt,
        audio_bitrate) < 0) {
        printf("无法初始化音频编码器\n");
        return -1;
    }

    printf("初始化完成：\n");
    printf("视频大小: %dx%d\n", ctx_encoder->width, ctx_encoder->height);
    printf("帧率: %d fps\n", fps);
    printf("码率: %d bps\n", bitrate);
    printf("音频采样率: %d Hz\n", ctx_encoder_a->sample_rate);
    printf("音频通道数: %d\n", ctx_encoder_a->channels);
    printf("音频码率: %d bps\n", audio_bitrate);


    // ctx_demuxer->sample_rate = ctx_decoder->dec_ctx->sample_rate;
    // ctx_demuxer->channels = ctx_decoder->dec_ctx->channels;
    // ctx_demuxer->sample_fmt = ctx_decoder->dec_ctx->sample_fmt;
    
    return 0;
}

// 清理资源的函数
void cleanup_decoder(Demuxer* ctx_demuxer) {
    if (ctx_demuxer) {
        // if (CTX_DEMUXER->output_file)
        //     fclose(CTX_DEMUXER->output_file);
        // if (CTX_DECODER->dec_ctx)
        //     avcodec_free_context(&CTX_DECODER->dec_ctx);
        if (ctx_demuxer->fmt_ctx)
            avformat_close_input(&ctx_demuxer->fmt_ctx);
        if (ctx_demuxer->pkt)
            av_packet_free(&ctx_demuxer->pkt);
        // if (CTX_DEMUXER->frame)
        //     av_frame_free(&CTX_DEMUXER->frame);
    }
}

// 这个函数有问题，莫名其妙显示重载
void extract_video_(Demuxer *ctx_demuxer ,char *input_filename, const char *output_filename,PacketQueue &vinq) {

}

// 上边的替代
void ext_video(Demuxer *ctx_demuxer,const char *input_filename,const char *output1_filename,const char *output2_filename,PacketQueue &vinq,PacketQueue &ainq){
    std::cout<<"video&audio_extract::"<<std::this_thread::get_id()<<":"<<std::endl;
    bool error_occurred = false;

    // 读取视频帧的循环
    while (!error_occurred && av_read_frame(ctx_demuxer->fmt_ctx, ctx_demuxer->pkt) >= 0) {
        if (ctx_demuxer->pkt->stream_index == ctx_demuxer->stream_index) {
            AVPacket* packet = av_packet_alloc();
            if (!packet) {
                printf("无法分配数据包\n");
                error_occurred = true;
                break;
            }
            
            // 复制数据包
            if (av_packet_ref(packet, ctx_demuxer->pkt) < 0) {
                printf("无法复制数据包\n");
                av_packet_free(&packet);
                error_occurred = true;
                break;
            }
            
            vinq.push(packet);
        }
        else if (ctx_demuxer->pkt->stream_index == ctx_demuxer->stream_index_a) {
            // 处理音频包
            AVPacket* packet = av_packet_alloc();
            if (!packet) {
                printf("无法分配音频数据包\n");
                error_occurred = true;
                break;
            }
            
            // 复制数据包
            if (av_packet_ref(packet, ctx_demuxer->pkt) < 0) {
                printf("无法复制音频数据包\n");
                av_packet_free(&packet);
                error_occurred = true;
                break;
            }
            
            ainq.push(packet);
        }
        av_packet_unref(ctx_demuxer->pkt);
    }

    // 发送结束信号到两个队列
    vinq.push(nullptr);
    ainq.push(nullptr);

    // 如果发生错误，通知队列停止
    if (error_occurred) {
        vinq.notify();
        ainq.notify();
    }

    // cleanup_decoder(ctx_demuxer);    
    // 打印播放命令
    print_ffplay_command(output1_filename, output2_filename,MEDIA_TYPE_VIDEO,ctx_demuxer);
}
