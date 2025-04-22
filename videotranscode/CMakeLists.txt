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
    src/video_decoder.cpp
    src/demuxer.cpp
    src/queue.cpp
    src/filter.cpp
    src/video_encoder.cpp
    src/audio_encoder.cpp
    src/muxer.cpp
)

# 创建可执行文件
add_executable(ffmpeg_test ${SOURCES})

# 链接 libffmpeg-zya.so 库和 SoundTouch 库
target_link_libraries(ffmpeg_test 
    ${CMAKE_SOURCE_DIR}/lib/libffmpeg-zya2.so
    ${CMAKE_SOURCE_DIR}/lib/libSoundTouch.so
)

# 设置运行时库路径（使用相对路径）
set_target_properties(ffmpeg_test PROPERTIES
    RPATH "${CMAKE_SOURCE_DIR}/lib"
)