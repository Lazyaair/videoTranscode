#include "encoder.h"
#include <stdio.h>
// 在Encoder结构体中添加重采样相关字段
static SwrContext *swr_ctx = nullptr;
static uint8_t **src_data = nullptr;
static uint8_t **dst_data = nullptr;
static int src_linesize, dst_linesize;
static int dst_nb_samples, max_dst_nb_samples;

int encoder_init_audio(Encoder *encoder, int sample_rate, int channels,
    enum AVSampleFormat sample_fmt, int bitrate) {
    int ret;

    // 查找AC3编码器
    encoder->codec = avcodec_find_encoder(AV_CODEC_ID_AC3);
    if (!encoder->codec) {
        printf("无法找到AC3编码器\n");
        return -1;
    }

    // 创建编码器上下文
    encoder->enc_ctx = avcodec_alloc_context3(encoder->codec);
    if (!encoder->enc_ctx) {
        printf("无法分配编码器上下文\n");
        return -1;
    }

    // 设置编码参数
    encoder->sample_rate = 48000;  // AC3要求48kHz采样率
    encoder->channels = 2;         // AC3使用立体声
    encoder->sample_fmt = AV_SAMPLE_FMT_FLTP;  // AC3要求浮点平面格式
    encoder->audio_bitrate = bitrate;
    encoder->next_pts = 0;

    // 设置编码器上下文参数
    encoder->enc_ctx->sample_fmt = AV_SAMPLE_FMT_FLTP;
    encoder->enc_ctx->sample_rate = 48000;
    encoder->enc_ctx->channels = 2;
    encoder->enc_ctx->channel_layout = AV_CH_LAYOUT_STEREO;
    encoder->enc_ctx->bit_rate = bitrate;
    encoder->enc_ctx->time_base = (AVRational){1, 48000};
    encoder->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    // 初始化重采样上下文
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        printf("无法分配重采样上下文\n");
        return -1;
    }

    // 设置重采样参数
    av_opt_set_int(swr_ctx, "in_channel_layout", av_get_default_channel_layout(channels), 0);
    av_opt_set_int(swr_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", sample_rate, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", 48000, 0);  // 确保输出是48000Hz
    av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", sample_fmt, 0);
    av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

    // // 设置高质量重采样参数
    // av_opt_set_int(swr_ctx, "linear_interp", 1, 0);        // 使用线性插值
    // av_opt_set_int(swr_ctx, "filter_size", 64, 0);         // 增加滤波器大小
    // av_opt_set_int(swr_ctx, "phase_shift", 8, 0);         // 相位偏移
    // av_opt_set_double(swr_ctx, "cutoff", 0.91, 0);          // 截止频率
    // // 使用高质量重采样引擎
    // av_opt_set_int(swr_ctx, "resampler", SWR_DITHER_TRIANGULAR, 0);

    // printf("已设置高质量重采样参数：\n");
    // printf("- 线性插值：开启\n");
    // printf("- 滤波器大小：64\n");
    // printf("- 相位偏移：8\n");
    // printf("- 截止频率：0.91\n");
    // printf("- Dither方法：三角形\n");

    // 初始化重采样上下文
    ret = swr_init(swr_ctx);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法初始化重采样上下文: %s\n", err_buf);
        return ret;
    }

    printf("编码重采样上下文初始化成功：\n");
    printf("编码输入采样率：%d Hz -> 输出采样率：48000 Hz\n", sample_rate);
    printf("编码输入通道数：%d -> 输出通道数：2\n", channels);
    printf("编码输入格式：%d -> 输出格式：FLTP\n", sample_fmt);

    // 打开编码器
    ret = avcodec_open2(encoder->enc_ctx, encoder->codec, NULL);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法打开编码器: %s\n", err_buf);
        return ret;
    }

    // 获取并打印帧大小
    int frame_size = encoder->enc_ctx->frame_size;
    printf("AC3编码器帧大小: %d\n", frame_size);

    // 分配编码帧
    encoder->frame = av_frame_alloc();
    if (!encoder->frame) {
        printf("无法分配编码帧\n");
        return -1;
    }

    // 设置帧参数
    encoder->frame->format = AV_SAMPLE_FMT_FLTP;
    encoder->frame->channel_layout = AV_CH_LAYOUT_STEREO;
    encoder->frame->sample_rate = 48000;
    encoder->frame->nb_samples = frame_size;  // 使用编码器的帧大小
    encoder->frame->channels = 2;

    // 分配帧缓冲区
    ret = av_frame_get_buffer(encoder->frame, 0);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法分配帧缓冲区: %s\n", err_buf);
        return ret;
    }

    // 分配重采样缓冲区
    max_dst_nb_samples = dst_nb_samples = 1024;
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, 2,
                                            max_dst_nb_samples, AV_SAMPLE_FMT_FLTP, 0);
    if (ret < 0) {
        printf("无法分配重采样目标缓冲区\n");
        return ret;
    }

    printf("AC3编码器初始化成功：\n");
    printf("编码输入采样率：%d Hz -> 输出采样率：48000 Hz\n", sample_rate);
    printf("编码输入通道数：%d -> 输出通道数：2\n", channels);
    printf("编码输入格式：%d -> 输出格式：FLTP\n", sample_fmt);
    printf("编码比特率：%d\n", bitrate);
    printf("编码帧大小：%d\n", frame_size);

    return 0;
}

int encoder_encode_audio(Encoder *encoder, AVFrame *frame, AVPacket *pkt) {
    int ret;
    static float **audio_buffer = nullptr;  // 改用float类型的缓冲区
    static int buffer_size = 0;
    static int samples_in_buffer = 0;
    const int required_samples = 1536;  // AC3要求的帧大小


    // 首次运行时初始化缓冲区
    if (!audio_buffer) {
        audio_buffer = (float**)av_calloc(2, sizeof(float*));  // 2个通道
        for (int ch = 0; ch < 2; ch++) {
            audio_buffer[ch] = (float*)av_calloc(required_samples * 4, sizeof(float));  // 预分配4帧的空间
        }
        buffer_size = required_samples * 4;
    }

    if (frame) {
        // 计算输出采样数
        dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, frame->sample_rate) +
                                      frame->nb_samples,
                                      48000, frame->sample_rate, AV_ROUND_UP);

        if (dst_nb_samples > max_dst_nb_samples) {
            av_freep(&dst_data[0]);
            ret = av_samples_alloc(dst_data, &dst_linesize, 2,
                                 dst_nb_samples, AV_SAMPLE_FMT_FLTP, 0);
            if (ret < 0) {
                printf("无法重新分配重采样缓冲区\n");
                return ret;
            }
            max_dst_nb_samples = dst_nb_samples;
        }

        // 执行重采样
        ret = swr_convert(swr_ctx, dst_data, dst_nb_samples,
                         (const uint8_t**)frame->data, frame->nb_samples);
        if (ret < 0) {
            printf("重采样失败\n");
            return ret;
        }

        // 检查是否需要扩展缓冲区
        if (samples_in_buffer + ret > buffer_size) {
            int new_size = (samples_in_buffer + ret) * 2;  // 倍增策略
            for (int ch = 0; ch < 2; ch++) {
                float* new_buf = (float*)av_calloc(new_size, sizeof(float));
                if (new_buf) {
                    memcpy(new_buf, audio_buffer[ch], samples_in_buffer * sizeof(float));
                    av_free(audio_buffer[ch]);
                    audio_buffer[ch] = new_buf;
                }
            }
            buffer_size = new_size;
        }

        // 分别复制每个通道的数据
        for (int ch = 0; ch < 2; ch++) {
            memcpy(audio_buffer[ch] + samples_in_buffer, 
                   dst_data[ch], 
                   ret * sizeof(float));
        }
        samples_in_buffer += ret;

        // // 设置编码帧参数
        // encoder->frame->nb_samples = ret;
        // av_frame_make_writable(encoder->frame);

        // // 复制重采样后的数据到编码帧
        // for (int ch = 0; ch < 2; ch++) {
        //     memcpy(encoder->frame->data[ch], dst_data[ch],
        //            ret * av_get_bytes_per_sample(AV_SAMPLE_FMT_FLTP));
        // }

        // encoder->frame->pts = encoder->next_pts;
        // encoder->next_pts += ret;
    }

    // 当有足够的采样点时，编码一帧
    while (samples_in_buffer >= required_samples) {
        // 设置编码帧参数
        av_frame_make_writable(encoder->frame);
        encoder->frame->nb_samples = required_samples;

        // 直接复制每个通道的数据
        for (int ch = 0; ch < 2; ch++) {
            memcpy(encoder->frame->data[ch],
                   audio_buffer[ch],
                   required_samples * sizeof(float));
        }

        // 更新PTS
        encoder->frame->pts = encoder->next_pts;
        encoder->next_pts += required_samples;

        // 发送帧到编码器
        ret = avcodec_send_frame(encoder->enc_ctx, encoder->frame);
        if (ret < 0) {
            printf("发送帧到编码器失败\n");
            return ret;
        }

        // 从编码器接收数据包
        ret = avcodec_receive_packet(encoder->enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            // 移动剩余的采样到缓冲区开始
            for (int ch = 0; ch < 2; ch++) {
                memmove(audio_buffer[ch],
                       audio_buffer[ch] + required_samples,
                       (samples_in_buffer - required_samples) * sizeof(float));
            }
            samples_in_buffer -= required_samples;
            return 0;
        } else if (ret < 0) {
            printf("从编码器接收数据包失败\n");
            return ret;
        }

        // 移动剩余的采样到缓冲区开始
        for (int ch = 0; ch < 2; ch++) {
            memmove(audio_buffer[ch],
                   audio_buffer[ch] + required_samples,
                   (samples_in_buffer - required_samples) * sizeof(float));
        }
        samples_in_buffer -= required_samples;
        return 1;
    }

    // 如果是最后一帧且缓冲区还有数据
    if (!frame && samples_in_buffer > 0) {
        // 设置编码帧参数
        av_frame_make_writable(encoder->frame);
        encoder->frame->nb_samples = required_samples;

        // 将剩余数据复制到编码帧，不足的部分补零
        for (int ch = 0; ch < 2; ch++) {
            memset(encoder->frame->data[ch], 0,
                required_samples * sizeof(float));
            memcpy(encoder->frame->data[ch],
                audio_buffer[ch],
                samples_in_buffer * sizeof(float));
        }

        encoder->frame->pts = encoder->next_pts;
        encoder->next_pts += required_samples;

        // 发送最后一帧
        ret = avcodec_send_frame(encoder->enc_ctx, encoder->frame);
        if (ret < 0) {
            printf("发送最后一帧到编码器失败\n");
            return ret;
        }

        // 接收编码后的数据包
        ret = avcodec_receive_packet(encoder->enc_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            samples_in_buffer = 0;
            return 0;
        } else if (ret < 0) {
            printf("从编码器接收最后的数据包失败\n");
            return ret;
        }

        samples_in_buffer = 0;
        return 1;
    }

    return 0;

    // // 发送帧到编码器
    // ret = avcodec_send_frame(encoder->enc_ctx, frame ? encoder->frame : NULL);
    // if (ret < 0) {
    //     printf("发送帧到编码器失败\n");
    //     return ret;
    // }

    // // 从编码器接收数据包
    // ret = avcodec_receive_packet(encoder->enc_ctx, pkt);
    // if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
    //     return 0;
    // } else if (ret < 0) {
    //     printf("从编码器接收数据包失败\n");
    //     return ret;
    // }

    // return 1;
}

// 添加清理函数
void encoder_cleanup(Encoder *encoder) {
    // 清理音频重采样相关资源
    if (swr_ctx) {
        swr_free(&swr_ctx);
        swr_ctx = nullptr;
    }
    if (dst_data) {
        if (dst_data[0]) {
            av_freep(&dst_data[0]);
        }
        av_freep(&dst_data);
    }

    // 清理通用编码器资源
    if (encoder->frame) {
        av_frame_free(&encoder->frame);
        encoder->frame = NULL;
    }
    
    if (encoder->enc_ctx) {
        avcodec_free_context(&encoder->enc_ctx);
        encoder->enc_ctx = NULL;
    }
    
    encoder->codec = NULL;
    encoder->next_pts = 0;
}

void encode_audio_thread(Encoder *encoder, FrametQueue& frame_queue, PacketQueue& packet_queue,float speed) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        printf("无法分配音频数据包\n");
        return;
    }

    bool encoding = true;   
    bool flush_needed = true;
    while (encoding) {
        // 从帧队列中获取帧
        AVFrame *frame = frame_queue.pop();
        if (!frame) {
            // 收到结束信号
            encoding = false;            
            continue;
        }
        
        // 在重采样参数中应用速度因子
        if (swr_ctx) {
            // 调整输出采样率以实现变速
            int out_sample_rate = 48000 * (1.0 / speed);
            av_opt_set_int(swr_ctx, "out_sample_rate", out_sample_rate, 0);
            
            // 重新初始化重采样上下文
            int ret = swr_init(swr_ctx);
            if (ret < 0) {
                char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
                printf("重新初始化重采样上下文失败: %s\n", err_buf);
                if (frame) {
                    av_frame_free(&frame);
                }
                continue;
            }
        }

        // 编码帧
        int ret = encoder_encode_audio(encoder, frame, pkt);
        if (ret < 0) {
            printf("音频编码失败，错误码: %d\n", ret);
            if (frame) {
                av_frame_free(&frame);
            }
            continue;
        } else if (ret > 0) {
            // 将编码后的数据包放入队列
            packet_queue.push(av_packet_clone(pkt));
            av_packet_unref(pkt);
        }

        // 释放帧
        if (frame) {
            av_frame_free(&frame);
        }
    }

    // 刷新编码器
    if (flush_needed) {
        printf("开始刷新音频编码器...\n");
        int flush_ret;
        do {
            flush_ret = encoder_encode_audio(encoder, nullptr, pkt);
            if (flush_ret > 0) {
                packet_queue.push(av_packet_clone(pkt));
                av_packet_unref(pkt);
            } else if (flush_ret < 0) {
                printf("刷新音频编码器时发生错误，错误码: %d\n", flush_ret);
                break;
            }
        } while (flush_ret > 0);
        printf("音频编码器刷新完成\n");
    }

    // 发送结束信号
    packet_queue.push(nullptr);

    // 清理资源
    av_packet_free(&pkt);
    printf("音频编码线程结束\n");
}
//======================================
// // 暂时没用
// int encoder_init_audio(Encoder *encoder, int sample_rate, int channels,
//     enum AVSampleFormat sample_fmt, int bitrate) {

//     int ret;

//     // 查找编码器
//     encoder->codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
//     if (!encoder->codec) {
//     printf("无法找到AAC编码器\n");
//     return -1;
//     }

//     // 创建编码器上下文
//     encoder->enc_ctx = avcodec_alloc_context3(encoder->codec);
//     if (!encoder->enc_ctx) {
//     printf("无法分配编码器上下文\n");
//     return -1;
//     }

//     // 设置编码参数
//     encoder->sample_rate = sample_rate;
//     encoder->channels = channels;
//     encoder->sample_fmt = sample_fmt;
//     encoder->audio_bitrate = bitrate;
//     encoder->next_pts = 0;

//     encoder->enc_ctx->sample_fmt = sample_fmt;
//     encoder->enc_ctx->sample_rate = sample_rate;
//     encoder->enc_ctx->channels = channels;
//     encoder->enc_ctx->channel_layout = av_get_default_channel_layout(channels);
//     encoder->enc_ctx->bit_rate = bitrate;
//     encoder->enc_ctx->time_base = (AVRational){1, sample_rate};
//     encoder->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

//     // 打开编码器
//     ret = avcodec_open2(encoder->enc_ctx, encoder->codec, NULL);
//     if (ret < 0) {
//     printf("无法打开编码器\n");
//     return ret;
//     }

//     // 分配编码帧
//     encoder->frame = av_frame_alloc();
//     if (!encoder->frame) {
//     printf("无法分配编码帧\n");
//     return -1;
//     }

//     encoder->frame->format = sample_fmt;
//     encoder->frame->channel_layout = encoder->enc_ctx->channel_layout;
//     encoder->frame->sample_rate = sample_rate;
//     encoder->frame->nb_samples = encoder->enc_ctx->frame_size;

//     // 分配帧缓冲区
//     ret = av_frame_get_buffer(encoder->frame, 0);
//     if (ret < 0) {
//     printf("无法分配帧缓冲区\n");
//     return ret;
//     }

//     return 0;
// }


// // 暂时没用
// int encoder_encode_audio(Encoder *encoder, AVFrame *frame, AVPacket *pkt) {
//     int ret;

//     if (frame) {
//         frame->pts = encoder->next_pts;
//         encoder->next_pts += frame->nb_samples;
//     }

//     // 发送帧到编码器
//     ret = avcodec_send_frame(encoder->enc_ctx, frame);
//     if (ret < 0) {
//         printf("发送帧到编码器失败\n");
//         return ret;
//     }

//     // 从编码器接收数据包
//     ret = avcodec_receive_packet(encoder->enc_ctx, pkt);
//     if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
//         return 0;
//     } else if (ret < 0) {
//         printf("从编码器接收数据包失败\n");
//         return ret;
//     }

//     return 1;
// }