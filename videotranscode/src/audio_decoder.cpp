// #include"decoder.h"
// #include"queue.h"
#include"demuxer.h"
#include"decoder.h"
#include"queue.h"
#include <iostream>

int audio_decode(const char *output_filename, Decoder* ctx_decoder, PacketQueue& ainq, FrametQueue& afq) {
    std::cout << "audio_decode::" << std::this_thread::get_id() << ":" << std::endl;
    bool error_occurred = false;
    AVPacket* packet = nullptr;
    SwrContext *swr_ctx = nullptr;
    uint8_t **dst_data = nullptr;
    int dst_linesize;
    int dst_nb_samples;
    int max_dst_nb_samples;
    int dst_channels = ctx_decoder->dec_ctx->channels;
    int dst_rate = ctx_decoder->dec_ctx->sample_rate;
    AVSampleFormat dst_format = AV_SAMPLE_FMT_S16;  // 目标格式为S16

    // 确保解码帧已分配
    if (!ctx_decoder->frame) {
        ctx_decoder->frame = av_frame_alloc();
        if (!ctx_decoder->frame) {
            printf("无法分配解码帧\n");
            return -1;
        }
    }

    // 打印音频解码器参数
    printf("音频解码器参数：\n");
    printf("输入采样格式：%d\n", ctx_decoder->dec_ctx->sample_fmt);
    printf("输入通道数：%d\n", ctx_decoder->dec_ctx->channels);
    printf("输入采样率：%d\n", ctx_decoder->dec_ctx->sample_rate);
    printf("输入通道布局：%lld\n", (long long)ctx_decoder->dec_ctx->channel_layout);

    // 打开输出文件（用于调试）
    ctx_decoder->output_file = fopen(output_filename, "wb");
    if (!ctx_decoder->output_file) {
        printf("无法打开音频输出文件\n");
        return -1;
    }

    // 如果输入格式不是S16，初始化重采样上下文
    if (ctx_decoder->dec_ctx->sample_fmt != AV_SAMPLE_FMT_S16) {
        printf("初始化音频重采样，输入格式：%d，输出格式：%d\n", 
               ctx_decoder->dec_ctx->sample_fmt, AV_SAMPLE_FMT_S16);

        // 确保通道布局有效
        int64_t in_ch_layout = ctx_decoder->dec_ctx->channel_layout;
        if (!in_ch_layout) {
            in_ch_layout = av_get_default_channel_layout(ctx_decoder->dec_ctx->channels);
            printf("使用默认输入通道布局：%lld\n", (long long)in_ch_layout);
        }
        
        int64_t out_ch_layout = av_get_default_channel_layout(dst_channels);
        
        printf("重采样参数：\n");
        printf("输入通道布局：%lld\n", (long long)in_ch_layout);
        printf("输出通道布局：%lld\n", (long long)out_ch_layout);
        printf("输入采样率：%d\n", ctx_decoder->dec_ctx->sample_rate);
        printf("输出采样率：%d\n", dst_rate);
        
        swr_ctx = swr_alloc();
        if (!swr_ctx) {
            printf("无法分配重采样上下文\n");
            error_occurred = true;
            goto end;
        }

        // 设置重采样参数
        av_opt_set_int(swr_ctx, "in_channel_layout",    in_ch_layout,             0);
        av_opt_set_int(swr_ctx, "out_channel_layout",   out_ch_layout,            0);
        av_opt_set_int(swr_ctx, "in_sample_rate",       ctx_decoder->dec_ctx->sample_rate, 0);
        av_opt_set_int(swr_ctx, "out_sample_rate",      dst_rate,                 0);
        av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt",  ctx_decoder->dec_ctx->sample_fmt, 0);
        av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_format,    0);

        // 初始化重采样上下文
        int ret = swr_init(swr_ctx);
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            printf("无法初始化重采样上下文: %s\n", err_buf);
            error_occurred = true;
            goto end;
        }

        // 分配重采样输出缓冲区
        max_dst_nb_samples = dst_nb_samples = 1024;  // 使用固定大小
        printf("分配重采样缓冲区，大小：%d 采样\n", max_dst_nb_samples);

        ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize,
                                             dst_channels, dst_nb_samples,
                                             dst_format, 0);
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            printf("无法分配重采样目标缓冲区: %s\n", err_buf);
            error_occurred = true;
            goto end;
        }
    }

    while (!error_occurred) {
        // 从队列中获取数据包
        packet = ainq.pop();
        if (!packet) {
            // 收到结束信号
            printf("音频解码收到结束信号\n");
            break;
        }

        // 发送数据包到解码器
        int ret = avcodec_send_packet(ctx_decoder->dec_ctx, packet);
        if (ret < 0) {
            char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
            av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
            printf("发送音频数据包到解码器失败: %s\n", err_buf);
            error_occurred = true;
            av_packet_free(&packet);
            break;
        }

        // 接收解码后的帧
        while (ret >= 0) {
            ret = avcodec_receive_frame(ctx_decoder->dec_ctx, ctx_decoder->frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
                av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
                printf("从解码器接收音频帧失败: %s\n", err_buf);
                error_occurred = true;
                break;
            }

            // printf("接收到音频帧：采样数=%d, 通道数=%d, 格式=%d\n",
            //        ctx_decoder->frame->nb_samples,
            //        ctx_decoder->frame->channels,
            //        ctx_decoder->frame->format);

            // 创建新帧
            AVFrame* frame_to_queue = av_frame_alloc();
            if (!frame_to_queue) {
                printf("无法分配音频帧\n");
                error_occurred = true;
                break;
            }

            // 如果需要重采样（用于PCM输出）
            if (swr_ctx) {
                // 计算输出采样数
                dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, ctx_decoder->dec_ctx->sample_rate) +
                                              ctx_decoder->frame->nb_samples,
                                              dst_rate, ctx_decoder->dec_ctx->sample_rate, AV_ROUND_UP);

                if (dst_nb_samples > max_dst_nb_samples) {
                    av_freep(&dst_data[0]);
                    ret = av_samples_alloc(dst_data, &dst_linesize, dst_channels,
                                         dst_nb_samples, dst_format, 1);
                    if (ret < 0) {
                        printf("无法重新分配重采样缓冲区\n");
                        error_occurred = true;
                        av_frame_free(&frame_to_queue);
                        break;
                    }
                    max_dst_nb_samples = dst_nb_samples;
                }

                // 执行重采样（用于PCM输出）
                ret = swr_convert(swr_ctx, dst_data, dst_nb_samples,
                                (const uint8_t**)ctx_decoder->frame->data,
                                ctx_decoder->frame->nb_samples);
                if (ret < 0) {
                    printf("重采样失败\n");
                    error_occurred = true;
                    av_frame_free(&frame_to_queue);
                    break;
                }

                // 计算PCM数据大小（每个采样2字节，乘以通道数）
                int bytes_per_sample = av_get_bytes_per_sample(dst_format);
                int buffer_size = ret * dst_channels * bytes_per_sample;

                // 写入PCM数据（用于调试）
                if (ctx_decoder->output_file) {
                    // 写入重采样后的数据
                    fwrite(dst_data[0], 1, buffer_size, ctx_decoder->output_file);
                }

                // 为队列克隆原始帧（不使用重采样后的帧）
                av_frame_free(&frame_to_queue);  // 释放之前分配的帧
                frame_to_queue = av_frame_clone(ctx_decoder->frame);
                if (!frame_to_queue) {
                    printf("无法克隆音频帧\n");
                    error_occurred = true;
                    break;
                }
            } else {
                // 不需要重采样，直接克隆原始帧
                frame_to_queue = av_frame_clone(ctx_decoder->frame);
                if (!frame_to_queue) {
                    printf("无法克隆音频帧\n");
                    error_occurred = true;
                    break;
                }
            }

            // 将原始帧放入队列
            afq.push(frame_to_queue);
        }

        av_packet_free(&packet);
    }

    // 发送结束信号
    afq.push(nullptr);

    // 如果发生错误，通知队列停止
    if (error_occurred) {
        afq.notify();
    }

end:
    // 清理资源
    if (swr_ctx) {
        swr_free(&swr_ctx);
    }
    if (dst_data) {
        av_freep(&dst_data[0]);
        av_freep(&dst_data);
    }
    if (ctx_decoder->output_file) {
        fclose(ctx_decoder->output_file);
        ctx_decoder->output_file = nullptr;
    }

    printf("音频解码线程结束\n");
    return error_occurred ? -1 : 0;
}




// //===========================================清晰版========================================================
// int audio_decode(const char *output_filename, Decoder* ctx_decoder, PacketQueue& ainq, FrametQueue& afq) {
//     std::cout << "audio_decode::" << std::this_thread::get_id() << ":" << std::endl;
//     bool error_occurred = false;
//     AVPacket* packet = nullptr;
//     SwrContext *swr_ctx = nullptr;
//     uint8_t **dst_data = nullptr;
//     int dst_linesize;
//     int dst_nb_samples;
//     int max_dst_nb_samples;
//     int dst_channels = ctx_decoder->dec_ctx->channels;
//     int dst_rate = ctx_decoder->dec_ctx->sample_rate;
//     AVSampleFormat dst_format = AV_SAMPLE_FMT_S16;  // 目标格式为S16

//     // 确保解码帧已分配
//     if (!ctx_decoder->frame) {
//         ctx_decoder->frame = av_frame_alloc();
//         if (!ctx_decoder->frame) {
//             printf("无法分配解码帧\n");
//             return -1;
//         }
//     }

//     // 打印音频解码器参数
//     printf("音频解码器参数：\n");
//     printf("解码输入采样格式：%d\n", ctx_decoder->dec_ctx->sample_fmt);
//     printf("解码输入通道数：%d\n", ctx_decoder->dec_ctx->channels);
//     printf("解码输入采样率：%d\n", ctx_decoder->dec_ctx->sample_rate);
//     printf("解码输入通道布局：%lld\n", (long long)ctx_decoder->dec_ctx->channel_layout);

//     // 打开输出文件（用于调试）
//     ctx_decoder->output_file = fopen(output_filename, "wb");
//     if (!ctx_decoder->output_file) {
//         printf("无法打开音频输出文件\n");
//         return -1;
//     }

//     // 如果输入格式不是S16，初始化重采样上下文
//     if (ctx_decoder->dec_ctx->sample_fmt != AV_SAMPLE_FMT_S16) {
//         printf("解码初始化音频重采样，输入格式：%d，输出格式：%d\n", 
//                ctx_decoder->dec_ctx->sample_fmt, AV_SAMPLE_FMT_S16);

//         // 确保通道布局有效
//         int64_t in_ch_layout = ctx_decoder->dec_ctx->channel_layout;
//         if (!in_ch_layout) {
//             in_ch_layout = av_get_default_channel_layout(ctx_decoder->dec_ctx->channels);
//             printf("解码使用默认输入通道布局：%lld\n", (long long)in_ch_layout);
//         }
        
//         int64_t out_ch_layout = av_get_default_channel_layout(dst_channels);
        
//         printf("解码重采样参数：\n");
//         printf("解码输入通道布局：%lld\n", (long long)in_ch_layout);
//         printf("解码输出通道布局：%lld\n", (long long)out_ch_layout);
//         printf("解码输入采样率：%d\n", ctx_decoder->dec_ctx->sample_rate);
//         printf("解码输出采样率：%d\n", dst_rate);
        
//         swr_ctx = swr_alloc();
//         if (!swr_ctx) {
//             printf("无法分配重采样上下文\n");
//             error_occurred = true;
//             goto end;
//         }

//         // 设置重采样参数
//         av_opt_set_int(swr_ctx, "in_channel_layout",    in_ch_layout,             0);
//         av_opt_set_int(swr_ctx, "out_channel_layout",   out_ch_layout,            0);
//         av_opt_set_int(swr_ctx, "in_sample_rate",       ctx_decoder->dec_ctx->sample_rate, 0);
//         av_opt_set_int(swr_ctx, "out_sample_rate",      dst_rate,                 0);
//         av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt",  ctx_decoder->dec_ctx->sample_fmt, 0);
//         av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", dst_format,    0);

//         // 初始化重采样上下文
//         int ret = swr_init(swr_ctx);
//         if (ret < 0) {
//             char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
//             av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
//             printf("无法初始化重采样上下文: %s\n", err_buf);
//             error_occurred = true;
//             goto end;
//         }

//         // 分配重采样输出缓冲区
//         max_dst_nb_samples = dst_nb_samples = 1024;  // 使用固定大小
//         printf("解码分配重采样缓冲区，大小：%d 采样\n", max_dst_nb_samples);

//         ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize,
//                                              dst_channels, dst_nb_samples,
//                                              dst_format, 0);
//         if (ret < 0) {
//             char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
//             av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
//             printf("无法分配重采样目标缓冲区: %s\n", err_buf);
//             error_occurred = true;
//             goto end;
//         }
//     }

//     while (!error_occurred) {
//         // 从队列中获取数据包
//         packet = ainq.pop();
//         if (!packet) {
//             // 收到结束信号
//             printf("音频解码收到结束信号\n");
//             break;
//         }

//         // 发送数据包到解码器
//         int ret = avcodec_send_packet(ctx_decoder->dec_ctx, packet);
//         if (ret < 0) {
//             char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
//             av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
//             printf("发送音频数据包到解码器失败: %s\n", err_buf);
//             error_occurred = true;
//             av_packet_free(&packet);
//             break;
//         }

//         // 接收解码后的帧
//         while (ret >= 0) {
//             ret = avcodec_receive_frame(ctx_decoder->dec_ctx, ctx_decoder->frame);
//             if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
//                 break;
//             } else if (ret < 0) {
//                 char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
//                 av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
//                 printf("从解码器接收音频帧失败: %s\n", err_buf);
//                 error_occurred = true;
//                 break;
//             }

//             // printf("接收到音频帧：采样数=%d, 通道数=%d, 格式=%d\n",
//             //        ctx_decoder->frame->nb_samples,
//             //        ctx_decoder->frame->channels,
//             //        ctx_decoder->frame->format);

//             // 创建新帧
//             AVFrame* frame_to_queue = av_frame_alloc();
//             if (!frame_to_queue) {
//                 printf("无法分配音频帧\n");
//                 error_occurred = true;
//                 break;
//             }

//             // 如果需要重采样
//             if (swr_ctx) {
//                 // 计算输出采样数
//                 dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, ctx_decoder->dec_ctx->sample_rate) +
//                                               ctx_decoder->frame->nb_samples,
//                                               dst_rate, ctx_decoder->dec_ctx->sample_rate, AV_ROUND_UP);

//                 if (dst_nb_samples > max_dst_nb_samples) {
//                     av_freep(&dst_data[0]);
//                     ret = av_samples_alloc(dst_data, &dst_linesize, dst_channels,
//                                          dst_nb_samples, dst_format, 1);
//                     if (ret < 0) {
//                         printf("无法重新分配重采样缓冲区\n");
//                         error_occurred = true;
//                         av_frame_free(&frame_to_queue);
//                         break;
//                     }
//                     max_dst_nb_samples = dst_nb_samples;
//                 }

//                 // 执行重采样
//                 ret = swr_convert(swr_ctx, dst_data, dst_nb_samples,
//                                 (const uint8_t**)ctx_decoder->frame->data,
//                                 ctx_decoder->frame->nb_samples);
//                 if (ret < 0) {
//                     printf("解码重采样失败\n");
//                     error_occurred = true;
//                     av_frame_free(&frame_to_queue);
//                     break;
//                 }

//                 // printf("重采样完成：输入采样数=%d, 输出采样数=%d\n",
//                 //        ctx_decoder->frame->nb_samples, ret);

//                 // 计算每个采样的字节数和总大小
//                 int bytes_per_sample = av_get_bytes_per_sample(dst_format);
//                 int buffer_size = ret * dst_channels * bytes_per_sample;

//                 // 写入PCM数据（用于调试）
//                 if (ctx_decoder->output_file) {
//                     // 直接写入重采样后的数据
//                     fwrite(dst_data[0], 1, buffer_size, ctx_decoder->output_file);
//                     fflush(ctx_decoder->output_file);
//                 }

//                 // 设置重采样后的帧参数
//                 frame_to_queue->format = dst_format;
//                 frame_to_queue->channel_layout = av_get_default_channel_layout(dst_channels);
//                 frame_to_queue->sample_rate = dst_rate;
//                 frame_to_queue->nb_samples = ret;
//                 frame_to_queue->channels = dst_channels;

//                 // 分配帧缓冲区
//                 ret = av_frame_get_buffer(frame_to_queue, 0);
//                 if (ret < 0) {
//                     printf("无法分配重采样后的帧缓冲区\n");
//                     av_frame_free(&frame_to_queue);
//                     error_occurred = true;
//                     break;
//                 }

//                 // 确保帧数据可写
//                 ret = av_frame_make_writable(frame_to_queue);
//                 if (ret < 0) {
//                     printf("无法使帧数据可写\n");
//                     av_frame_free(&frame_to_queue);
//                     error_occurred = true;
//                     break;
//                 }

//                 // 复制重采样后的数据到帧
//                 memcpy(frame_to_queue->data[0], dst_data[0], buffer_size);
//             } else {
//                 // 不需要重采样，直接复制原始帧
//                 frame_to_queue->format = ctx_decoder->frame->format;
//                 frame_to_queue->channel_layout = ctx_decoder->frame->channel_layout;
//                 frame_to_queue->sample_rate = ctx_decoder->frame->sample_rate;
//                 frame_to_queue->nb_samples = ctx_decoder->frame->nb_samples;
//                 frame_to_queue->channels = ctx_decoder->frame->channels;

//                 ret = av_frame_get_buffer(frame_to_queue, 0);
//                 if (ret < 0) {
//                     printf("无法分配帧缓冲区\n");
//                     av_frame_free(&frame_to_queue);
//                     error_occurred = true;
//                     break;
//                 }

//                 ret = av_frame_copy(frame_to_queue, ctx_decoder->frame);
//                 if (ret < 0) {
//                     printf("无法复制帧数据\n");
//                     av_frame_free(&frame_to_queue);
//                     error_occurred = true;
//                     break;
//                 }
//             }

//             frame_to_queue->pts = ctx_decoder->frame->pts;
//             afq.push(frame_to_queue);
//         }

//         av_packet_free(&packet);
//     }

//     // 发送结束信号
//     afq.push(nullptr);

//     // 如果发生错误，通知队列停止
//     if (error_occurred) {
//         afq.notify();
//     }

// end:
//     // 清理资源
//     if (swr_ctx) {
//         swr_free(&swr_ctx);
//     }
//     if (dst_data) {
//         av_freep(&dst_data[0]);
//         av_freep(&dst_data);
//     }
//     if (ctx_decoder->output_file) {
//         fclose(ctx_decoder->output_file);
//         ctx_decoder->output_file = nullptr;
//     }

//     printf("音频解码线程结束\n");
//     return error_occurred ? -1 : 0;
// }




//============================================噪音版========================================================
// int audio_decode(const char *output_filename, Decoder* ctx_decoder, PacketQueue& ainq, FrametQueue& afq) {
//     std::cout << "audio_decode::" << std::this_thread::get_id() << ":" << std::endl;
//     bool error_occurred = false;
//     AVPacket* packet = nullptr;

//     // 打开输出文件（用于调试）
//     ctx_decoder->output_file = fopen(output_filename, "wb");
//     if (!ctx_decoder->output_file) {
//         printf("无法打开音频输出文件\n");
//         return -1;
//     }

//     while (!error_occurred) {
//         // 从队列中获取数据包
//         packet = ainq.pop();
//         if (!packet) {
//             // 收到结束信号
//             printf("音频解码收到结束信号\n");
//             break;
//         }

//         // 发送数据包到解码器
//         int ret = avcodec_send_packet(ctx_decoder->dec_ctx, packet);
//         if (ret < 0) {
//             printf("发送音频数据包到解码器失败: %d\n", ret);
//             error_occurred = true;
//             av_packet_free(&packet);
//             break;
//         }

//         // 接收解码后的帧
//         while (ret >= 0) {
//             ret = avcodec_receive_frame(ctx_decoder->dec_ctx, ctx_decoder->frame);
//             if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
//                 // 需要更多数据或已到达文件末尾
//                 break;
//             } else if (ret < 0) {
//                 printf("从解码器接收音频帧失败: %d\n", ret);
//                 error_occurred = true;
//                 break;
//             }

//             // 创建新帧并复制解码后的音频数据
//             AVFrame* frame_to_queue = av_frame_alloc();
//             if (!frame_to_queue) {
//                 printf("无法分配音频帧\n");
//                 error_occurred = true;
//                 break;
//             }

//             // 设置帧参数
//             frame_to_queue->format = ctx_decoder->frame->format;
//             frame_to_queue->nb_samples = ctx_decoder->frame->nb_samples;
//             frame_to_queue->channel_layout = ctx_decoder->frame->channel_layout;
//             frame_to_queue->channels = ctx_decoder->frame->channels;
//             frame_to_queue->sample_rate = ctx_decoder->frame->sample_rate;
//             frame_to_queue->pts = ctx_decoder->frame->pts;

//             // 分配帧缓冲区
//             ret = av_frame_get_buffer(frame_to_queue, 0);
//             if (ret < 0) {
//                 printf("无法分配音频帧缓冲区\n");
//                 av_frame_free(&frame_to_queue);
//                 error_occurred = true;
//                 break;
//             }

//             // 确保帧是可写的
//             ret = av_frame_make_writable(frame_to_queue);
//             if (ret < 0) {
//                 printf("无法使音频帧可写\n");
//                 av_frame_free(&frame_to_queue);
//                 error_occurred = true;
//                 break;
//             }

//             // 复制音频数据
//             ret = av_frame_copy(frame_to_queue, ctx_decoder->frame);
//             if (ret < 0) {
//                 printf("无法复制音频帧数据\n");
//                 av_frame_free(&frame_to_queue);
//                 error_occurred = true;
//                 break;
//             }

//             // 写入PCM数据（用于调试）
//             int data_size = av_get_bytes_per_sample((AVSampleFormat)frame_to_queue->format);
//             for (int i = 0; i < frame_to_queue->nb_samples; i++) {
//                 for (int ch = 0; ch < frame_to_queue->channels; ch++) {
//                     fwrite(frame_to_queue->data[ch] + data_size * i, 1, data_size, ctx_decoder->output_file);
//                 }
//             }

//             // 将帧放入队列
//             afq.push(frame_to_queue);
//         }

//         // 释放数据包
//         av_packet_free(&packet);
//     }

//     // 发送结束信号
//     afq.push(nullptr);

//     // 如果发生错误，通知队列停止
//     if (error_occurred) {
//         afq.notify();
//     }

//     // 关闭输出文件
//     if (ctx_decoder->output_file) {
//         fclose(ctx_decoder->output_file);
//         ctx_decoder->output_file = nullptr;
//     }

//     printf("音频解码线程结束\n");
//     return error_occurred ? -1 : 0;
// }
