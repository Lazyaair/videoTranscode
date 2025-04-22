// #include <iostream>
#include"decoder.h"
#include <stdio.h>
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}
#include<thread>


//在decoder时将帧穿过滤镜
int video_decode(const char *output_filename,Decoder* ctx_decoder,PacketQueue&vinq,FrametQueue& vfq){
    std::cout<<"video_decode::"<<std::this_thread::get_id()<<":"<<std::endl;
    // 打开输出文件
    // 分配解码帧、旋转帧
    ctx_decoder->frame = av_frame_alloc();
    AVFrame* rotated_frame = av_frame_alloc();
    if (!ctx_decoder->frame) {
        printf("无法分配解码帧\n");
        return -1;
    }

    // 打开输出文件
    ctx_decoder->output_file = fopen(output_filename, "wb");
    if (!ctx_decoder->output_file) {
        printf("无法打开输出文件\n");
        av_frame_free(&ctx_decoder->frame);
        return -1;
    }

    AVPacket* packet = nullptr;
    int ret;
    bool decoding = true;

    // 初始化旋转器
    VideoRotator* rotator = rotator_create();
    if (!rotator) {
        printf("无法创建旋转器\n");
        av_frame_free(&ctx_decoder->frame);
        av_frame_free(&rotated_frame);
        return -1;
    }

    // printf("初始化旋转器参数: width=%d, height=%d, pix_fmt=%d\n",
    //     ctx_decoder->dec_ctx->width,
    //     ctx_decoder->dec_ctx->height,
    //     ctx_decoder->dec_ctx->pix_fmt);

    if (rotator_init(rotator, 
                     ctx_decoder->dec_ctx->width, 
                     ctx_decoder->dec_ctx->height,
                     ctx_decoder->dec_ctx->pix_fmt,
                     ctx_decoder->dec_ctx->time_base,
                     ctx_decoder->dec_ctx->sample_aspect_ratio) < 0) {
        printf("无法初始化旋转器\n");
        av_frame_free(&ctx_decoder->frame);
        av_frame_free(&rotated_frame);
        rotator_destroy(rotator);
        return -1;
    }
    
    // 读取并解码所有数据包
    while (decoding) {
        packet = vinq.pop();

        if (!packet) {
            // 收到结束信号
            decoding = false;
            // 冲洗解码器
            avcodec_send_packet(ctx_decoder->dec_ctx, NULL);
        } else {
            // packet->pts = packet->pts / speed;
            // packet->dts = packet->dts / speed;
            // packet->duration = packet->duration / speed;
            ret = avcodec_send_packet(ctx_decoder->dec_ctx, packet);
            if (ret < 0) {
                printf("发送数据包到解码器失败: %d\n", ret);
                av_packet_free(&packet);
                break;
            }
            av_packet_free(&packet);
        }

        // 接收并处理解码后的帧
        while (1) {
            ret = avcodec_receive_frame(ctx_decoder->dec_ctx, ctx_decoder->frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                // 需要更多数据或已到达文件末尾
                break;
            } else if (ret < 0) {
                printf("从解码器接收帧失败: %d\n", ret);
                decoding = false;
                break;
            }
            // filter旋转过滤
            if (rotator_rotate_frame(rotator, ctx_decoder->frame, rotated_frame) < 0) {
                printf("旋转帧失败\n");
                continue;
            }
            // 创建新帧并复制旋转后的帧数据
            AVFrame* frame_to_queue = av_frame_alloc();
            if (!frame_to_queue) {
                printf("无法分配新帧\n");
                continue;
            }

            // 设置帧参数
            frame_to_queue->format = rotated_frame->format;
            frame_to_queue->width = rotated_frame->width;
            frame_to_queue->height = rotated_frame->height;
            frame_to_queue->pts = rotated_frame->pts;
            // 确保宽度是16的倍数（x264要求）
            int aligned_width = (frame_to_queue->width + 15) & ~15;  // 将436对齐到448
            // printf("原始宽度: %d, 对齐后宽度: %d\n", frame_to_queue->width, aligned_width);

            // 计算对齐后的步幅
            frame_to_queue->linesize[0] = aligned_width;
            frame_to_queue->linesize[1] = aligned_width / 2;
            frame_to_queue->linesize[2] = aligned_width / 2;

            // 分配帧缓冲区，使用32字节对齐
            ret = av_frame_get_buffer(frame_to_queue, 32);
            if (ret < 0) {
                printf("无法分配帧缓冲区\n");
                av_frame_free(&frame_to_queue);
                continue;
            }

            // 确保帧是可写的
            ret = av_frame_make_writable(frame_to_queue);
            if (ret < 0) {
                printf("无法使帧可写\n");
                av_frame_free(&frame_to_queue);
                continue;
            }
            // 分配帧缓冲区
            ret = av_frame_get_buffer(frame_to_queue, 0);
            if (ret < 0) {
                printf("无法分配帧缓冲区\n");
                av_frame_free(&frame_to_queue);
                continue;
            }


            // rotated_frame->pts = rotated_frame->pts/2.0;
            // 复制帧数据
            ret = av_frame_copy(frame_to_queue, rotated_frame);
            if (ret < 0) {
                printf("无法复制帧数据\n");
                av_frame_free(&frame_to_queue);
                continue;
            }

            // // 1. 将pts转换为以秒为单位的时间戳
            // double pts_seconds = rotated_frame->pts * av_q2d(ctx_decoder->dec_ctx->time_base);
            // // 2. 应用速度因子
            // pts_seconds = pts_seconds * (1.0 / speed);
            // // 3. 转回以time_base为单位的pts
            // frame_to_queue->pts = (int64_t)(pts_seconds / av_q2d(ctx_decoder->dec_ctx->time_base));
            // 将帧放入队列
            vfq.push(frame_to_queue);


            // // 写入原始YUV数据
            // fwrite(ctx_decoder->frame->data[0], 1, ctx_decoder->frame->linesize[0] * ctx_decoder->dec_ctx->height, ctx_decoder->output_file);
            // fwrite(ctx_decoder->frame->data[1], 1, ctx_decoder->frame->linesize[1] * ctx_decoder->dec_ctx->height / 2, ctx_decoder->output_file);
            // fwrite(ctx_decoder->frame->data[2], 1, ctx_decoder->frame->linesize[2] * ctx_decoder->dec_ctx->height / 2, ctx_decoder->output_file);
            // 注意：旋转90度后，原来的宽变成了高，原来的高变成了宽
            int rotated_width = rotated_frame->width;   // 旋转后的宽度
            int rotated_height = rotated_frame->height; // 旋转后的高度

            // Y平面
            for (int i = 0; i < rotated_height; i++) {
                fwrite(rotated_frame->data[0] + i * rotated_frame->linesize[0], 
                      1, rotated_width, ctx_decoder->output_file);
            }

            // U平面
            for (int i = 0; i < rotated_height/2; i++) {
                fwrite(rotated_frame->data[1] + i * rotated_frame->linesize[1], 
                      1, rotated_width/2, ctx_decoder->output_file);
            }

            // V平面
            for (int i = 0; i < rotated_height/2; i++) {
                fwrite(rotated_frame->data[2] + i * rotated_frame->linesize[2], 
                      1, rotated_width/2, ctx_decoder->output_file);
            }

            // 释放旋转后的帧
            av_frame_unref(rotated_frame);
        }
    }
    // 发送结束信号
    vfq.push(nullptr);
    // 清理资源
    if (ctx_decoder->output_file) {
        fclose(ctx_decoder->output_file);
        ctx_decoder->output_file = NULL;
    }
    if (ctx_decoder->frame) {
        av_frame_free(&ctx_decoder->frame);
        ctx_decoder->frame = NULL;
    }

    printf("视频提取完成！\n");
    return 0;
}
