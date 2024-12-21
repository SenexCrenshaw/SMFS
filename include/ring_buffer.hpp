#pragma once
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

class RingBuffer
{
public:
    explicit RingBuffer(size_t capacity)
        : capacity_(capacity)
    {
        buffer_.resize(capacity_);
    }

    // push data, blocking if full
    bool push(const char *data, size_t len, std::atomic<bool> &stop)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (size_t i = 0; i < len; i++)
        {
            condNotFull_.wait(lock, [&]
                              { return (size_ < capacity_) || stop.load(); });

            if (stop.load())
            {
                return false; // stop requested
            }

            buffer_[(head_ + size_) % capacity_] = data[i];
            size_++;
            condNotEmpty_.notify_one();
        }
        return true;
    }

    // pop data, blocking if empty
    size_t pop(char *dest, size_t len, std::atomic<bool> &stop)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t bytesRead = 0;
        while (bytesRead < len)
        {
            while (size_ == 0 && !stop.load())
            {
                condNotEmpty_.wait(lock);
            }

            if (stop.load() && size_ == 0)
            {
                break; // no data left
            }

            if (size_ > 0)
            {
                dest[bytesRead] = buffer_[head_];
                head_ = (head_ + 1) % capacity_;
                size_--;
                bytesRead++;
                condNotFull_.notify_one();
            }
            else
            {
                break;
            }
        }
        return bytesRead;
    }

    // clear the buffer
    void clear()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_ = 0;
        head_ = 0;
        condNotFull_.notify_all();
        condNotEmpty_.notify_all();
    }

    // Get the current size of the buffer
    size_t size()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return size_;
    }

private:
    std::vector<char> buffer_;
    size_t capacity_ = 0;

    size_t head_ = 0; // read position
    size_t size_ = 0; // how many bytes are in the buffer

    std::mutex mutex_;
    std::condition_variable condNotEmpty_;
    std::condition_variable condNotFull_;
};
