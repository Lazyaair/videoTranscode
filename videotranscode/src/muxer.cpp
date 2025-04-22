
#include "muxer.h"
#include <stdio.h>


int muxer_init(Muxer *muxer, const char *filename) {
    int ret;
    
    // 初始化成员变量
    muxer->fmt_ctx = NULL;
    muxer->video_stream = NULL;
    muxer->audio_stream = NULL;
    muxer->filename = filename;
    muxer->have_video = 0;
    muxer->have_audio = 0;
    muxer->next_pts = 0;
    muxer->audio_next_pts = 0;

    // 创建输出上下文
    avformat_alloc_output_context2(&muxer->fmt_ctx, NULL, NULL, filename);
    if (!muxer->fmt_ctx) {
        printf("无法创建输出上下文\n");
        return -1;
    }

    return 0;
}


// 参数配置
int muxer_add_video_stream(Muxer *muxer, int width, int height, 
                          enum AVPixelFormat pix_fmt, int fps) {
    if (!muxer->fmt_ctx) {
        return -1;
    }

    muxer->video_stream = avformat_new_stream(muxer->fmt_ctx, NULL);
    if (!muxer->video_stream) {
        printf("无法创建视频流\n");
        return -1;
    }

    // 设置视频流参数
    muxer->width = width;
    muxer->height = height;
    muxer->pix_fmt = pix_fmt;
    muxer->fps = fps;
    muxer->have_video = 1;

    // 设置时基和帧率
    muxer->video_stream->time_base = (AVRational){1, fps};
    muxer->video_stream->avg_frame_rate = (AVRational){fps, 1};
    muxer->video_stream->r_frame_rate = (AVRational){fps, 1};

    // 设置编解码器参数
    muxer->video_stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    muxer->video_stream->codecpar->codec_id = AV_CODEC_ID_H264;
    muxer->video_stream->codecpar->width = width;
    muxer->video_stream->codecpar->height = height;
    muxer->video_stream->codecpar->format = pix_fmt;
    muxer->video_stream->codecpar->bit_rate = 400000;  // 设置合适的码率

    // 设置视频流标志
    muxer->video_stream->disposition = AV_DISPOSITION_DEFAULT;
    return 0;
}



int muxer_add_audio_stream(Muxer *muxer, int sample_rate, int channels,
                          enum AVSampleFormat sample_fmt) {
    // 创建音频流
    muxer->audio_stream = avformat_new_stream(muxer->fmt_ctx, NULL);
    if (!muxer->audio_stream) {
        printf("无法创建音频流\n");
        return -1;
    }

    // 设置音频流参数
    muxer->sample_rate = sample_rate;
    muxer->channels = channels;
    muxer->sample_fmt = sample_fmt;
    muxer->have_audio = 1;

    // 设置时基为1/48000（AC3标准采样率）
    muxer->audio_stream->time_base = AVRational{1, 48000};

    // 设置编解码器参数
    muxer->audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    muxer->audio_stream->codecpar->codec_id = AV_CODEC_ID_AC3;
    muxer->audio_stream->codecpar->sample_rate = 48000;  // AC3要求48kHz
    muxer->audio_stream->codecpar->channels = 2;         // AC3使用立体声
    muxer->audio_stream->codecpar->format = AV_SAMPLE_FMT_FLTP;
    muxer->audio_stream->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
    muxer->audio_stream->codecpar->bit_rate = 192000;   // 设置默认音频比特率

    printf("已添加AC3音频流：\n");
    printf("采样率：48000 Hz\n");
    printf("通道数：2 (立体声)\n");
    printf("采样格式：FLTP\n");
    printf("比特率：192 kbps\n");

    return 0;
}

int muxer_write_header(Muxer *muxer) {
    int ret;

    // 打开输出文件
    if (!(muxer->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&muxer->fmt_ctx->pb, muxer->filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            printf("无法打开输出文件\n");
            return ret;
        }
    }

    // 写入文件头
    ret = avformat_write_header(muxer->fmt_ctx, NULL);
    if (ret < 0) {
        printf("写入文件头失败\n");
        return ret;
    }

    return 0;
}

int muxer_write_packet(Muxer *muxer, AVPacket *pkt) {
    return av_interleaved_write_frame(muxer->fmt_ctx, pkt);
}

int muxer_write_trailer(Muxer *muxer) {
    return av_write_trailer(muxer->fmt_ctx);
}

void muxer_cleanup(Muxer *muxer) {
    if (muxer->fmt_ctx && !(muxer->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&muxer->fmt_ctx->pb);
    }
    
    if (muxer->fmt_ctx) {
        avformat_free_context(muxer->fmt_ctx);
        muxer->fmt_ctx = NULL;
    }
    
    muxer->video_stream = NULL;
    muxer->audio_stream = NULL;
    muxer->have_video = 0;
    muxer->have_audio = 0;
}

void muxer_thread(const char *output_filename, Encoder* ctx_encoder_v, Encoder* ctx_encoder_a,PacketQueue& v_out_q, PacketQueue& a_out_q, Muxer* muxer) {
    printf("开始运行封装线程\n");
    
    // 初始化封装器
    if (muxer_init(muxer, output_filename) < 0) {
        printf("无法初始化封装器\n");
        return;
    }

    // 添加视频流
    if (muxer_add_video_stream(muxer, 
        ctx_encoder_v->width,
        ctx_encoder_v->height,
        ctx_encoder_v->pix_fmt,
        ctx_encoder_v->fps) < 0) {
        printf("无法添加视频流\n");
        return;
    }

    // 添加音频流
    if (muxer_add_audio_stream(muxer,
        ctx_encoder_a->sample_rate,
        ctx_encoder_a->channels,
        ctx_encoder_a->sample_fmt) < 0) {
        printf("无法添加音频流\n");
        return;
    }

    // 复制视频编码器extradata到封装器
    if (ctx_encoder_v->enc_ctx->extradata_size > 0) {
        muxer->video_stream->codecpar->extradata_size = ctx_encoder_v->enc_ctx->extradata_size;
        muxer->video_stream->codecpar->extradata = (uint8_t *)av_mallocz(ctx_encoder_v->enc_ctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(muxer->video_stream->codecpar->extradata, ctx_encoder_v->enc_ctx->extradata, ctx_encoder_v->enc_ctx->extradata_size);
        printf("已复制视频编码器extradata，大小：%d\n", ctx_encoder_v->enc_ctx->extradata_size);
    }

    // 复制音频编码器extradata到封装器
    if (ctx_encoder_a->enc_ctx->extradata_size > 0) {
        muxer->audio_stream->codecpar->extradata_size = ctx_encoder_a->enc_ctx->extradata_size;
        muxer->audio_stream->codecpar->extradata = (uint8_t *)av_mallocz(ctx_encoder_a->enc_ctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        memcpy(muxer->audio_stream->codecpar->extradata, ctx_encoder_a->enc_ctx->extradata, ctx_encoder_a->enc_ctx->extradata_size);
        printf("已复制音频编码器extradata，大小：%d\n", ctx_encoder_a->enc_ctx->extradata_size);
    }

    // 写入文件头
    if (muxer_write_header(muxer) < 0) {
        printf("无法写入文件头\n");
        return;
    }

    bool muxing = true;
    bool video_finished = false;
    bool audio_finished = false;

    while (muxing) {
        // 如果两个流都结束了，退出循环
        if (video_finished && audio_finished) {
            printf("视频和音频流都已结束\n");
            break;
        }

        // 处理视频包
        if (!video_finished) {
            AVPacket* v_packet = v_out_q.pop();
            if (!v_packet) {
                video_finished = true;
                printf("视频流结束\n");
            } else {
                // 设置流索引
                v_packet->stream_index = muxer->video_stream->index;

                // 转换视频时间戳
                AVStream* v_stream = muxer->video_stream;
                AVRational v_in_tb = ctx_encoder_v->enc_ctx->time_base;
                AVRational v_out_tb = v_stream->time_base;

                v_packet->pts = av_rescale_q_rnd(v_packet->pts,
                                          v_in_tb, v_out_tb,
                                          (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                v_packet->dts = av_rescale_q_rnd(v_packet->dts,
                                          v_in_tb, v_out_tb,
                                          (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                v_packet->duration = av_rescale_q(v_packet->duration,
                                          v_in_tb, v_out_tb);

                // 写入视频包
                if (muxer_write_packet(muxer, v_packet) < 0) {
                    printf("写入视频包失败\n");
                }
                av_packet_free(&v_packet);
            }
        }

        // 处理音频包
        if (!audio_finished) {
            AVPacket* a_packet = a_out_q.pop();
            if (!a_packet) {
                audio_finished = true;
                printf("音频流结束\n");
            } else {
                // 设置流索引
                a_packet->stream_index = muxer->audio_stream->index;

                // 转换音频时间戳
                AVStream* a_stream = muxer->audio_stream;
                AVRational a_in_tb = ctx_encoder_a->enc_ctx->time_base;
                AVRational a_out_tb = a_stream->time_base;

                a_packet->pts = av_rescale_q_rnd(a_packet->pts,
                                          a_in_tb, a_out_tb,
                                          (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                a_packet->dts = av_rescale_q_rnd(a_packet->dts,
                                          a_in_tb, a_out_tb,
                                          (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                a_packet->duration = av_rescale_q(a_packet->duration,
                                          a_in_tb, a_out_tb);

                // 写入音频包
                if (muxer_write_packet(muxer, a_packet) < 0) {
                    printf("写入音频包失败\n");
                }
                av_packet_free(&a_packet);
            }
        }
    }

    // 写入文件尾
    if (muxer_write_trailer(muxer) < 0) {
        printf("无法写入文件尾\n");
    }

    // 清理资源
    muxer_cleanup(muxer);
    printf("封装线程结束\n");
}
//=================================封装视频
// void muxer_thread( const char *output_filename,Encoder* ctx_encoder, PacketQueue& v_out_q, Muxer* muxer) {
//     printf("开始运行封装线程\n");
    
//     // 初始化封装器
//     if (muxer_init(muxer, output_filename) < 0) {
//         printf("无法初始化封装器\n");
//         return;
//     }

//     // 添加视频流
//     if (muxer_add_video_stream(muxer, 
//         ctx_encoder->width,
//         ctx_encoder->height,
//         ctx_encoder->pix_fmt,
//         ctx_encoder->fps) < 0) {
//         printf("无法添加视频流\n");
//         return;
//     }

//     // 复制编码器extradata到封装器
//     if (ctx_encoder->enc_ctx->extradata_size > 0) {
//         muxer->video_stream->codecpar->extradata_size = ctx_encoder->enc_ctx->extradata_size;
//         muxer->video_stream->codecpar->extradata = (uint8_t *)av_mallocz(ctx_encoder->enc_ctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
//         memcpy(muxer->video_stream->codecpar->extradata, ctx_encoder->enc_ctx->extradata, ctx_encoder->enc_ctx->extradata_size);
//         printf("已复制编码器extradata，大小：%d\n", ctx_encoder->enc_ctx->extradata_size);
//     } else {
//         printf("警告：编码器extradata为空\n");
//     }

//     // 写入文件头
//     if (muxer_write_header(muxer) < 0) {
//         printf("无法写入文件头\n");
//         return;
//     }

//     bool muxing = true;
//     while (muxing) {
//         // 从队列中获取数据包
//         AVPacket* packet = v_out_q.pop();
//         if (!packet) {
//             // 收到结束信号
//             printf("收到封装结束信号\n");
//             muxing = false;
//             continue;
//         }

//         // 设置流索引
//         packet->stream_index = muxer->video_stream->index;

//         // 转换时间戳
//         AVStream* out_stream = muxer->video_stream;
//         AVRational in_time_base = ctx_encoder->enc_ctx->time_base;
//         AVRational out_time_base = out_stream->time_base;

//         packet->pts = av_rescale_q_rnd(packet->pts,
//                                       in_time_base,
//                                       out_time_base,
//                                       (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        
//         packet->dts = av_rescale_q_rnd(packet->dts,
//                                       in_time_base,
//                                       out_time_base,
//                                       (AVRounding)(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        
//         packet->duration = av_rescale_q(packet->duration,
//                                       in_time_base,
//                                       out_time_base);

//         // 写入数据包
//         int ret = muxer_write_packet(muxer, packet);
//         if (ret < 0) {
//             char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
//             av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
//             printf("写入数据包失败: %s\n", err_buf);
//             av_packet_free(&packet);
//             break;
//         }

//         // 释放数据包
//         av_packet_free(&packet);
//     }

//     // 写入文件尾
//     if (muxer_write_trailer(muxer) < 0) {
//         printf("无法写入文件尾\n");
//     }

//     // 清理资源
//     muxer_cleanup(muxer);
//     printf("封装线程结束\n");
// }