#include "queue.h"


//packet队列
PacketQueue::PacketQueue() : abort_request_(false) {}

PacketQueue::~PacketQueue() {
    clear();
}

void PacketQueue::push(AVPacket* packet) {
    std::unique_lock<std::mutex> lock(mutex_);
    queue_.push(packet);
    cond_.notify_one();
}

AVPacket* PacketQueue::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (queue_.empty() && !abort_request_) {
        cond_.wait(lock);
    }
    
    if (abort_request_) {
        return nullptr;
    }
    
    AVPacket* packet = queue_.front();
    queue_.pop();
    return packet;
}

void PacketQueue::clear() {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
        AVPacket* packet = queue_.front();
        av_packet_free(&packet);
        queue_.pop();
    }
    abort_request_ = false;
}

int PacketQueue::size() {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
}

void PacketQueue::notify() {
    std::unique_lock<std::mutex> lock(mutex_);
    abort_request_ = true;
    cond_.notify_all();
} 

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