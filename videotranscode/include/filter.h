#pragma once

extern "C" {
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libavfilter/avfilter.h"
    #include "libavfilter/buffersrc.h"
    #include "libavfilter/buffersink.h"
    #include "libavutil/opt.h"
}

typedef struct VideoRotator {
    AVFilterGraph* filter_graph;
    AVFilterContext* buffersrc_ctx;
    AVFilterContext* buffersink_ctx;
    int initialized;
} VideoRotator;

// 初始化旋转器
VideoRotator* rotator_create();

// 销毁旋转器
void rotator_destroy(VideoRotator* rotator);

// 初始化滤镜
int rotator_init(VideoRotator* rotator, int width, int height, 
                AVPixelFormat pix_fmt, AVRational time_base, 
                AVRational sample_aspect_ratio);

// 处理一帧视频
int rotator_rotate_frame(VideoRotator* rotator, AVFrame* in_frame, AVFrame* out_frame);

// 清理资源
void rotator_cleanup(VideoRotator* rotator);