#include "encoder.h"
#include <stdio.h>

int encoder_init_video(Encoder *encoder, int width, int height, 
                      enum AVPixelFormat pix_fmt, int fps, int bitrate) {
    int ret;

    // 查找编码器
    encoder->codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!encoder->codec) {
        printf("无法找到H264编码器\n");
        return -1;
    }
    // 确保宽度是16的倍数（x264要求）
    int aligned_width = (width + 15) & ~15;
    printf("编码器 - 原始宽度: %d, 对齐后宽度: %d\n", width, aligned_width);

    // 创建编码器上下文
    encoder->enc_ctx = avcodec_alloc_context3(encoder->codec);
    if (!encoder->enc_ctx) {
        printf("无法分配编码器上下文\n");
        return -1;
    }

    // 设置编码参数
    encoder->width = width;
    encoder->height = height;
    encoder->pix_fmt = pix_fmt;
    encoder->fps = fps;
    encoder->bitrate = bitrate;
    encoder->next_pts = 0;

    encoder->enc_ctx->width = width;
    encoder->enc_ctx->height = height;
    encoder->enc_ctx->pix_fmt = pix_fmt;
    // encoder->enc_ctx->time_base = AVRational{1, fps};
    encoder->enc_ctx->time_base = (AVRational){1, 1000};  // 使用毫秒时基
    encoder->enc_ctx->framerate = AVRational{fps, 1};
    encoder->enc_ctx->bit_rate = bitrate;
    encoder->enc_ctx->gop_size = fps;
    encoder->enc_ctx->max_b_frames = 0;
    encoder->enc_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // encoder->enc_ctx->thread_count = 4;  // 使用多线程编码
    encoder->enc_ctx->thread_type = FF_THREAD_SLICE;  // 使用切片级线程    
    encoder->enc_ctx->thread_count = 1;  // 暂时使用单线程编码
    
    // 设置profile和level
    encoder->enc_ctx->profile = FF_PROFILE_H264_HIGH;
    encoder->enc_ctx->level = 41; // 4.1级别，支持1080p
    // 对于H264，设置更多详细参数
    AVDictionary *opts = NULL;
    av_dict_set(&opts, "preset", "medium", 0);
    av_dict_set(&opts, "tune", "zerolatency", 0);
    av_dict_set(&opts, "x264opts", "keyint=24:min-keyint=24:no-scenecut", 0);
    av_dict_set(&opts, "profile", "high", 0);
    av_dict_set(&opts, "level", "4.1", 0);

    // 打开编码器
    ret = avcodec_open2(encoder->enc_ctx, encoder->codec, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        printf("无法打开编码器\n");
        return ret;
    }

    printf("视频编码器extradata_size: %d\n", encoder->enc_ctx->extradata_size);
    if (encoder->enc_ctx->extradata_size > 0) {
        printf("编码器extradata已设置\n");
    } else {
        printf("警告：编码器extradata为空\n");
    }

    // 分配编码帧
    encoder->frame = av_frame_alloc();
    if (!encoder->frame) {
        printf("无法分配编码帧\n");
        return -1;
    }


    encoder->frame->format = pix_fmt;
    encoder->frame->width = aligned_width;  // 使用对齐后的宽度
    encoder->frame->height = height;

    // 设置步幅
    encoder->frame->linesize[0] = aligned_width;
    encoder->frame->linesize[1] = aligned_width / 2;
    encoder->frame->linesize[2] = aligned_width / 2;

    // 分配帧缓冲区
    ret = av_frame_get_buffer(encoder->frame, 32);  // 使用32字节对齐
    if (ret < 0) {
        printf("无法分配帧缓冲区\n");
        return ret;
    }
    return 0;
}


int encoder_encode_video(Encoder *encoder, AVFrame *frame, AVPacket *pkt) {
    int ret;

    // if (frame) {
    //     frame->pts = encoder->next_pts++;
    // }

    // 发送帧到编码器
    ret = avcodec_send_frame(encoder->enc_ctx, frame);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("发送帧到编码器失败: %s\n", err_buf);
        return ret;
    }

    // 从编码器接收数据包
    ret = avcodec_receive_packet(encoder->enc_ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
    } else if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("从编码器接收数据包失败: %s\n", err_buf);
        return ret;
    }

    return 1;
}



int encoder_flush(Encoder *encoder, AVPacket *pkt) {
    int ret;

    // 发送NULL帧来刷新缓冲区
    ret = avcodec_send_frame(encoder->enc_ctx, NULL);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("刷新编码器失败: %s\n", err_buf);
    }

    // 接收剩余的数据包
    ret = avcodec_receive_packet(encoder->enc_ctx, pkt);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
        return 0;
    } else if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("从编码器接收数据包失败: %s\n", err_buf);
        return ret;
    }

    return 1;
}

// void encoder_cleanup(Encoder *encoder) {
//     if (encoder->frame) {
//         av_frame_free(&encoder->frame);
//         encoder->frame = NULL;
//     }
    
//     if (encoder->enc_ctx) {
//         avcodec_free_context(&encoder->enc_ctx);
//         encoder->enc_ctx = NULL;
//     }
    
//     encoder->codec = NULL;
//     encoder->next_pts = 0;
// }

void encode_thread(Encoder *encoder, FrametQueue& frame_queue, PacketQueue& packet_queue,float speed) {
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        printf("无法分配数据包\n");
        return;
    }

    bool encoding = true;   
    bool flush_needed = true;  // 添加标志来追踪是否需要刷新
    int64_t pts_counter = 0;  // 使用64位整数计数器

    while (encoding) {
        // 从帧队列中获取帧
        AVFrame *frame = frame_queue.pop();
        if (!frame) {
            // 收到结束信号
            encoding = false;            
            continue;  // 直接进入下一次循环，让刷新逻辑单独处理
        }

        // 使用90kHz时基计算PTS
        frame->pts = pts_counter;
        // 计算下一帧的PTS增量：1000(毫秒) / (fps * speed)
        int64_t pts_increment = (int64_t)(1000.0 / (encoder->fps * speed) + 0.5);  // 加0.5进行四舍五入
        pts_counter += pts_increment;


        // printf("除speed前frame->pts: %d\n", frame->pts);
        // frame->pts=frame->pts/speed;
        // printf("编码前frame->pts: %d\n", frame->pts);

        // 编码帧
        int ret = encoder_encode_video(encoder, frame, pkt);
        // printf("编码后pkt->pts: %d\n", frame->pts);
        if (ret < 0) {
            printf("编码帧失败，错误码: %d\n", ret);
            if (frame) {
                av_frame_free(&frame);
            }
            continue;
        } else if (ret > 0) {
            // 调整packet的时间戳
            if (pkt->pts != AV_NOPTS_VALUE) {
                AVRational dst_time_base = {1, 1000};
                pkt->pts = av_rescale_q(pkt->pts, encoder->enc_ctx->time_base, dst_time_base);
            }
            if (pkt->dts != AV_NOPTS_VALUE) {
                AVRational dst_time_base = {1, 1000};
                pkt->dts = av_rescale_q(pkt->dts, encoder->enc_ctx->time_base, dst_time_base);
            }
            // 将编码后的数据包放入队列
            packet_queue.push(av_packet_clone(pkt));
            av_packet_unref(pkt);
        }

        // 释放帧
        if (frame) {
            av_frame_free(&frame);
        }
    }

    // 只在需要时进行刷新
    if (flush_needed) {
        printf("开始刷新视频编码器...\n");
        int flush_ret;
        do {
            flush_ret = encoder_flush(encoder, pkt);
            if (flush_ret > 0) {
                packet_queue.push(av_packet_clone(pkt));
                av_packet_unref(pkt);
            } else if (flush_ret < 0) {
                printf("刷新编码器时发生错误，错误码: %d\n", flush_ret);
                break;
            }
        } while (flush_ret > 0);
        printf("视频编码器刷新完成\n");
    }

    // 发送结束信号
    packet_queue.push(nullptr);

    // 清理资源
    av_packet_free(&pkt);
    printf("编码线程结束\n");
}