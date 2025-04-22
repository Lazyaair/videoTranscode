# day3

1. 重构 frame 队列底层实现，实现环形缓冲区
   [queue.h](./include/queue.h)

   ```C++
   template<typename T>
   class RingBuffer {
   private:
      T** buffer;
      size_t capacity;
      size_t head;  // 读取位置
      size_t tail;  // 写入位置
      size_t size_;

   public:
      RingBuffer(size_t cap = 512) : capacity(cap), head(0), tail(0), size_(0) {
         buffer = new T*[capacity];
      }

      ~RingBuffer() {
         delete[] buffer;
      }

      bool push(T* val) {
         if (size_ >= capacity) {
               // 缓冲区已满
               return false;
         }
         buffer[tail] = val;
         tail = (tail + 1) % capacity;
         size_++;
         return true;
      }

      T* front() const {
         if (empty()) {
               return nullptr;
         }
         return buffer[head];
      }

      void pop() {
         if (!empty()) {
               head = (head + 1) % capacity;
               size_--;
         }
      }

      bool empty() const {
         return size_ == 0;
      }

      size_t size() const {
         return size_;
      }
   };
   ```

   实现[queue.cpp](./src/queue.cpp)

   ```C++
   //frame队列
   FrametQueue::FrametQueue() : abort_request_(false) {}

   FrametQueue::~FrametQueue() {
      clear();
   }

   void FrametQueue::push(AVFrame* frame) {
      std::unique_lock<std::mutex> lock(mutex_);
      // 等待直到有空间可用
      while (!abort_request_ && !queue_.push(frame)) {
         cond_.wait(lock);
      }

      if (abort_request_) {
         av_frame_free(&frame);
         return;
      }

      cond_.notify_one();  // 通知等待的消费者
   }

   AVFrame* FrametQueue::pop() {
      std::unique_lock<std::mutex> lock(mutex_);
      while (queue_.empty() && !abort_request_) {
         cond_.wait(lock);
      }

      if (abort_request_) {
         return nullptr;
      }

      AVFrame* frame = queue_.front();
      queue_.pop();
      cond_.notify_one();  // 通知等待的生产者
      return frame;
   }

   void FrametQueue::clear() {
      std::unique_lock<std::mutex> lock(mutex_);
      while (!queue_.empty()) {
         AVFrame* frame = queue_.front();
         av_frame_free(&frame);
         queue_.pop();
      }
      abort_request_ = false;
   }

   int FrametQueue::size() {
      std::unique_lock<std::mutex> lock(mutex_);
      return queue_.size();
   }

   void FrametQueue::notify() {
      std::unique_lock<std::mutex> lock(mutex_);
      abort_request_ = true;
      cond_.notify_all();
   }
   ```

2. 加入音频处理部分,并且可以正确播放音画同步的视频文件  
   `ffplay -f s16le -ar 44100 -ac 2 audio.pcm`正常语速清晰的音频[audio.pcm](./build/audio.pcm)
   ----[audio_decoder](./src/audio_decoder.cpp)  
   ----[audio_encoder](./src/audio_encoder.cpp)  
   同时调整传参
3. 实现变速功能  
   在[audio_decoder](./src/audio_decoder.cpp)实现对解到帧重采样，并将原始帧发给[audio_encoder](./src/audio_encoder.cpp)重采样后写入[audio.pcm](./build/audio.pcm)做调试验证  
   [audio_encoder](./src/audio_encoder.cpp)拿出帧后重采样编码为 AC3 格式  
   音频使用以下实现

   ```C++
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
   ```

   视频则计算 pts 时现  
   展示中选取 0.5、1、2 三个倍速演示，由第五个参数指定
   ![录像](./media/bigjob.mp4)  
   音频变速不变调和音话同步未能一起实现,故而回档未记录代码  
   ![录像](./media/音频不变调展示.mp4)

4. 整体实现一个主线程、六个子线程[Transcode.cpp](./src/Transcode.cpp)非常直观的展示出来了

# **day2**

1. 编写队列代码  
   [queue.h](./include/queue.h)  
   [queue.cpp](./src/queue.cpp)  
   在线程那节课作业写过线程安全队列，当时使用 std::queue+std::mutex+condition_varibl..  
   结构不变，取最基本的几个功能，push、pop……  
   将 std::queue 重新自己写一个，方法变，这里使用链表作底层，因为帧、包都按序读取，没有其他要求，也好实现  
   在因为 frame 也要传递，并且没要求，所以复制了一份 framepacket

2. 我是按顺序写，解封装-->解码-->滤镜-->编码-->复封装 顺序，这里查看的是，由滤镜拿出的翻转过后的 yuv 视频

3. 把帧从滤镜拿出后，送入 framequeue，然后由编码器拿出，编码后再送入另一个 packetqueue，另一端时 muxer

4. muxer 这里要将 encoder 中的编码器编码器 extradata 正确复制，不然会播放不了

5. **录屏**
   ![视频](./media/记录.mp4)
   ![收集视频](./media/VID_20250309_170950.mp4)

6. 当前架构  
   一个主线程、4 个子线程  
   首先对其初始化由于可能线程间都要使用  
   muxer 在线程中初始化即可  
   涉及到的全局数据

```C++
PacketQueue V_IN_Q;    // 视频包入队列
PacketQueue A_IN_Q;    // 音频包入队列
// VideoRotator* R_T_R;    // 滤镜//不需要直接在decoder中局部构造
FrametQueue V_F_Q;  // 视频帧队列
FrametQueue A_F_Q;  // 音频帧队列
PacketQueue V_OUT_Q;   // 视频包出队列
PacketQueue A_OUT_Q;   // 音频包出队列
Decoder* CTX_DECODER=nullptr;  // 解码器上下文
Demuxer* CTX_DEMUXER=nullptr;  // 解复用器上下文
Encoder* CTX_ENCODER=nullptr;   //编码器上下文
Muxer*   CTX_MUXER =nullptr;    //封装上下文
```

```
                                             transcode线程
                                                   |
                                                   |
                                                init_all
                                                   |
                                                   |
                                   ---------------------------------------
                                   |          |              |           |
                                   t1         t2             t3         t4
```

# day1

1. **ffmpegf 库编译**  
   首先编译静态库脚本文件[static.sh](./lib/buildstatic.sh)  
   踩坑编译动态库时额外库没有禁用也没有连接，卡了很久

```bash
./configure \
  --enable-static \
  --disable-shared \
  --enable-pic \
  --prefix=/home/ubuntu2204/workspace/ffmpeg/zyalib \
  --enable-gpl \
  --enable-libx264 \
  --enable-libx265 \
  --enable-nonfree

make
make install
```

然后链接为动态库[so.sh](./lib/buildso.sh)

```bash
gcc -shared -o libffmpeg-zya2.so \
    -Wl,-Bsymbolic \
    -Wl,--whole-archive \
    libavcodec.a \
    libavdevice.a \
    libavfilter.a \
    libavformat.a \
    libavutil.a \
    libpostproc.a \
    libswresample.a \
    libswscale.a \
    -Wl,--no-whole-archive \
    -fPIC \
    -lSDL2 \
    -lX11 -lXext -lXv -lxcb \
    -lx264 -lx265 \
    -lasound \
    -lsndio \
    -lm -lz -lpthread -ldl
```

2. **CMakeLists.txt 组织项目**
   将 ffmpeg 所用.h 头文件粘过来与自己的组织放在[./include](./include/)下

```c
cmake_minimum_required(VERSION 3.10)
project(FFmpegTest)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 添加头文件目录
include_directories(${CMAKE_SOURCE_DIR}/include)

# 添加源文件
set(SOURCES
    src/Transcode.cpp
    src/audio_decoder.cpp
    src/audio_packet.cpp
    src/video_decoder.cpp
    src/video_packet.cpp
    src/demuxer.cpp
)

# 创建可执行文件
add_executable(ffmpeg_test ${SOURCES})

# 链接 libffmpeg-zya.so 库
target_link_libraries(ffmpeg_test ${CMAKE_SOURCE_DIR}/lib/libffmpeg-zya2.so)

# 设置运行时库路径（使用相对路径）
set_target_properties(ffmpeg_test PROPERTIES
    RPATH "${CMAKE_SOURCE_DIR}/lib"
)
```

3. **ffmpeg 生成音视频文件**  
   | 头文件 | CPP |
   | :---: | :---: |
   | [decoder.h](./include/decoder.h) | [audio_decoder.cpp](./src/audio_decoder.cpp) |
   | [demuxer.h](./include/demuxer.h) | [video_decoder.cpp](./src/video_decoder.cpp) |
   | [queue.h](./include/queue.h) | [Transcode.cpp](./src/Transcode.cpp) |
   | -| [demuxer.cpp](./src/demuxer.cpp) |

   代码：一开始想通过类来实现，但是编译过程中用掉太多时间，在实现区分音视频帧之后的结构没有想好，遂追求函数实现，分别实现 mp4->yuv\pcm，  
   然后提取相同部分，按功能写，由于没有使用队列，解封装 demuxer 部分没有使用单独线程去运行，分别将两个 decoder 实现线程调用

   ---[视频](./media/video.webm)
   **音频有杂音，声音需放大一些可以听到**
   ---[音频](./media/audio.webm)
