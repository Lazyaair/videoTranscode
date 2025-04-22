#include <stdio.h>
#include <stdlib.h>
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include "libswresample/swresample.h"
}
#include "decoder.h"
#include "demuxer.h"
#include "encoder.h"
#include "muxer.h"
#include "filter.h"
#include <thread>

PacketQueue V_IN_Q;    // 视频包入队列
PacketQueue A_IN_Q;    // 音频包入队列
// VideoRotator* R_T_R;    // 滤镜//不需要直接在decoder中局部构造
FrametQueue V_F_Q;  // 视频帧队列
FrametQueue A_F_Q;  // 音频帧队列
PacketQueue V_OUT_Q;   // 视频包出队列
PacketQueue A_OUT_Q;   // 音频包出队列
Decoder* CTX_DECODER=nullptr;  // 视频解码器上下文
Decoder* CTX_DECODER_A=nullptr;  // 音频解码器上下文
Demuxer* CTX_DEMUXER=nullptr;  // 解复用器上下文 
Encoder* CTX_ENCODER=nullptr;   //视频编码器上下文
Encoder* CTX_ENCODER_A=nullptr;   //音频编码器上下文
Muxer*   CTX_MUXER =nullptr;    //封装上下文

int main(int argc, char *argv[]) {
    int ret = -1;
    std::thread t1, t2, t3,t4,t5,t6;  // 声明在最开始
    //开启线程
    printf("开始\n");
    // 参数规定：a1=1.mp4 a2=video.yuv a3=audio.pcm a4=output.pu4 a5=speed
    if (argc < 1) {
        printf("Usage: %s <input.mp4> <output.yuv> <output.pcm>\n", argv[0]);
        return -1;
    }
    char *endptr;
    float speed = strtof(argv[5], &endptr);

    // 检查转换是否成功
    if (*endptr != '\0') {
        printf("Error: Invalid float number: %s\n", argv[1]);
        return 1;
    }
    // 分配内存
    CTX_DECODER = (Decoder*)av_mallocz(sizeof(Decoder));
    CTX_DECODER_A = (Decoder*)av_mallocz(sizeof(Decoder));
    CTX_DEMUXER = (Demuxer*)av_mallocz(sizeof(Demuxer));
    CTX_ENCODER = (Encoder*)av_mallocz(sizeof(Encoder));
    CTX_ENCODER_A = (Encoder*)av_mallocz(sizeof(Encoder));
    CTX_MUXER   = (Muxer*)av_mallocz(sizeof(Muxer));
    
    if (!CTX_DECODER || !CTX_DEMUXER) {
        printf("无法分配内存\n");
        goto end;
    }

    // 初始化解码器
    if (init_all(argv[1], argv[3], MEDIA_TYPE_VIDEO, CTX_DEMUXER, CTX_DECODER,CTX_ENCODER,CTX_DECODER_A,CTX_ENCODER_A) < 0) {
        printf("初始化失败\n");
        goto end;
    }
    
    //开启线程
    printf("开启线程\n");
    // 创建线程
    t1 = std::thread(ext_video, CTX_DEMUXER, argv[1], argv[2],argv[3], std::ref(V_IN_Q),std::ref(A_IN_Q));  //demuxer
    t2 = std::thread(video_decode, argv[2], CTX_DECODER, std::ref(V_IN_Q),std::ref(V_F_Q)); //V---decoder
    t3 =std::thread(encode_thread,CTX_ENCODER,std::ref(V_F_Q),std::ref(V_OUT_Q),speed);   //V---encoder
    t4 =std::thread(muxer_thread,argv[4],CTX_ENCODER,CTX_ENCODER_A,std::ref(V_OUT_Q),std::ref(A_OUT_Q),CTX_MUXER);  //muxer
    t5 = std::thread(audio_decode, argv[3], CTX_DECODER_A, std::ref(A_IN_Q),std::ref(A_F_Q));   //A---decoder
    t6 =std::thread(encode_audio_thread,CTX_ENCODER_A,std::ref(A_F_Q),std::ref(A_OUT_Q),speed);  //A---encoder
    // std::thread t2(extract_audio,argv[1], argv[3]);
    if (t1.joinable())
    {
        t1.join();
    }
    if (t2.joinable())
    {
        t2.join();
    }
    if (t3.joinable())
    {
        t3.join();
    }
    if (t4.joinable())
    {
        t4.join();
    }
    if (t5.joinable())
    {
        t5.join();
    }
    if (t6.joinable())
    {
        t6.join();
    }
    ret = 0;  // 成功完成
end:
    // 清理资源
       // 清理解码器
       if (CTX_DECODER) {
        if (CTX_DECODER->dec_ctx) {
            avcodec_free_context(&CTX_DECODER->dec_ctx);
        }
        if (CTX_DECODER->frame) {
            av_frame_free(&CTX_DECODER->frame);
        }
        av_free(CTX_DECODER);
    }
    
    // 清理音频解码器
    if (CTX_DECODER_A) {
        if (CTX_DECODER_A->dec_ctx) {
            avcodec_free_context(&CTX_DECODER_A->dec_ctx);
        }
        if (CTX_DECODER_A->frame) {
            av_frame_free(&CTX_DECODER_A->frame);
        }
        av_free(CTX_DECODER_A);
    }
    
    // 清理编码器
    if (CTX_ENCODER) {
        encoder_cleanup(CTX_ENCODER);
        av_free(CTX_ENCODER);
    }
    
    // 清理音频编码器
    if (CTX_ENCODER_A) {
        encoder_cleanup(CTX_ENCODER_A);
        av_free(CTX_ENCODER_A);
    }
    
    // 清理解复用器
    if (CTX_DEMUXER) {
        cleanup_decoder(CTX_DEMUXER);
        av_free(CTX_DEMUXER);
    }
    
    // 清理封装器
    if (CTX_MUXER) {
        muxer_cleanup(CTX_MUXER);
        av_free(CTX_MUXER);
    }
    return ret;
}

// #include "demuxer.h"
// #include<iostream>
// int main() {
//     Demuxer demuxer;
    
//     // 打开文件
//     if (demuxer.open("../src/1.mp4") < 0) {
//         std::cerr << "无法打开文件" << std::endl;
//         return -1;
//     }

//     // 读取帧
//     AVPacket* packet = av_packet_alloc();
//     bool is_video;
//     while (demuxer.readFrame(packet, is_video) >= 0) {
//         if (is_video) {
//             std::cout << "读取到视频帧, PTS: " << packet->pts << std::endl;
//         } else {
//             std::cout << "读取到音频帧, PTS: " << packet->pts << std::endl;
//         }

//         av_packet_unref(packet); // 释放数据
//     }

//     av_packet_free(&packet);
//     demuxer.close();
//     std::cout << "关闭" << std::endl;

//     return 0;
// }
