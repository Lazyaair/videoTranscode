#include "filter.h"
#include <stdio.h>
#include <stdlib.h>

VideoRotator* rotator_create() {
    VideoRotator* rotator = (VideoRotator*)malloc(sizeof(VideoRotator));
    if (!rotator) {
        return NULL;
    }
    rotator->filter_graph = NULL;
    rotator->buffersrc_ctx = NULL;
    rotator->buffersink_ctx = NULL;
    rotator->initialized = 0;
    return rotator;
}

void rotator_destroy(VideoRotator* rotator) {
    if (rotator) {
        rotator_cleanup(rotator);
        free(rotator);
    }
}

int rotator_init(VideoRotator* rotator, int width, int height, 
    AVPixelFormat pix_fmt, AVRational time_base, 
    AVRational sample_aspect_ratio) {
    char args[512];
    int ret = 0;

    // 参数验证
    if (width <= 0 || height <= 0) {
        printf("无效的视频尺寸: %dx%d\n", width, height);
        return -1;
    }

    // 创建滤镜图
    rotator->filter_graph = avfilter_graph_alloc();
    if (!rotator->filter_graph) {
        printf("无法创建滤镜图\n");
        return -1;
    }

    // 创建输入输出滤镜
    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) {
        printf("无法找到必要的滤镜: buffer=%p buffersink=%p\n", buffersrc, buffersink);
        return -1;
    }

    // 创建输入滤镜上下文
    snprintf(args, sizeof(args),
    "width=%d:height=%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
    width, height, pix_fmt,
    time_base.num, time_base.den,
    sample_aspect_ratio.num, sample_aspect_ratio.den);

    // printf("滤镜参数: %s\n", args);  // 添加调试输出

    ret = avfilter_graph_create_filter(&rotator->buffersrc_ctx, buffersrc, "in",
                            args, NULL, rotator->filter_graph);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法创建buffer源: %s\n", err_buf);
        return ret;
    }

    // 创建输出滤镜上下文
    ret = avfilter_graph_create_filter(&rotator->buffersink_ctx, buffersink, "out",
                            NULL, NULL, rotator->filter_graph);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法创建buffer sink: %s\n", err_buf);
        return ret;
    }

    // 设置输出像素格式
    enum AVPixelFormat pix_fmts[] = { pix_fmt, AV_PIX_FMT_NONE };
    ret = av_opt_set_int_list(rotator->buffersink_ctx, "pix_fmts", pix_fmts,
                    AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法设置输出像素格式: %s\n", err_buf);
        return ret;
    }

    // 创建滤镜图描述
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        printf("无法分配滤镜连接点\n");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = rotator->buffersrc_ctx;
    outputs->pad_idx = 0;
    outputs->next = NULL;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = rotator->buffersink_ctx;
    inputs->pad_idx = 0;
    inputs->next = NULL;

    // 配置旋转滤镜
    ret = avfilter_graph_parse_ptr(rotator->filter_graph, "transpose=1",
                        &inputs, &outputs, NULL);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法解析滤镜图: %s\n", err_buf);
        goto end;
    }

    ret = avfilter_graph_config(rotator->filter_graph, NULL);
    if (ret < 0) {
        char err_buf[AV_ERROR_MAX_STRING_SIZE] = {0};
        av_strerror(ret, err_buf, AV_ERROR_MAX_STRING_SIZE);
        printf("无法配置滤镜图: %s\n", err_buf);
        goto end;
    }

    rotator->initialized = 1;
    printf("滤镜初始化成功\n");

end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    if (ret < 0) {
        rotator_cleanup(rotator);
    }
    return ret;
}

int rotator_rotate_frame(VideoRotator* rotator, AVFrame* in_frame, AVFrame* out_frame) {
    if (!rotator->initialized) {
        return -1;
    }

    int ret;

    // 将帧发送到源滤镜
    ret = av_buffersrc_add_frame_flags(rotator->buffersrc_ctx, in_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        printf("错误: 无法将帧送入滤镜图\n");
        return ret;
    }

    // 从sink滤镜获取帧
    ret = av_buffersink_get_frame(rotator->buffersink_ctx, out_frame);
    if (ret < 0) {
        printf("错误: 无法从滤镜图获取帧\n");
        return ret;
    }

    return 0;
}

void rotator_cleanup(VideoRotator* rotator) {
    if (rotator && rotator->filter_graph) {
        avfilter_graph_free(&rotator->filter_graph);
        rotator->filter_graph = NULL;
        rotator->buffersrc_ctx = NULL;
        rotator->buffersink_ctx = NULL;
        rotator->initialized = 0;
    }
} 