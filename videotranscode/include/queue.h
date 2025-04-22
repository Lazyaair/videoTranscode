#pragma once
#include <mutex>
#include <condition_variable>
extern "C" {
#include "libavcodec/avcodec.h"
}

// 自定义队列节点
template<typename T>
struct Node {
    T data;
    Node* next;
    Node(const T& value) : data(value), next(nullptr) {}
};

// 自定义队列模板类
template<typename T>
class MyQueue {
private:
    Node<T>* front_;
    Node<T>* rear_;
    int size_;

public:
    MyQueue() : front_(nullptr), rear_(nullptr), size_(0) {}
    
    ~MyQueue() {
        while (!empty()) {
            pop();
        }
    }

    void push(const T& value) {
        Node<T>* newNode = new Node<T>(value);
        if (empty()) {
            front_ = rear_ = newNode;
        } else {
            rear_->next = newNode;
            rear_ = newNode;
        }
        size_++;
    }

    T front() const {
        if (empty()) {
            throw std::runtime_error("Queue is empty");
        }
        return front_->data;
    }

    void pop() {
        if (empty()) {
            return;
        }
        Node<T>* temp = front_;
        front_ = front_->next;
        delete temp;
        size_--;
        if (size_ == 0) {
            rear_ = nullptr;
        }
    }

    bool empty() const {
        return size_ == 0;
    }

    int size() const {
        return size_;
    }
};


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

// PacketQueue类声明
class PacketQueue {
public:
    PacketQueue();
    ~PacketQueue();

    // 将packet放入队列
    void push(AVPacket* packet);
    // 从队列中取出packet，如果队列为空则等待
    AVPacket* pop();
    // 清空队列
    void clear();
    // 获取队列大小
    int size();
    // 通知队列停止等待
    void notify();

private:
    MyQueue<AVPacket*> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool abort_request_;
};

// AVFrameQueue类声明
class FrametQueue {
public:
    FrametQueue();
    ~FrametQueue();

    // 将packet放入队列
    void push(AVFrame* packet);
    // 从队列中取出packet，如果队列为空则等待
    AVFrame* pop();
    // 清空队列
    void clear();
    // 获取队列大小
    int size();
    // 通知队列停止等待
    void notify();

private:
    RingBuffer<AVFrame> queue_;
    std::mutex mutex_;
    std::condition_variable cond_;
    bool abort_request_;
};