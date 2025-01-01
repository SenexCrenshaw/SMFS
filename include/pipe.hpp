#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include "logger.hpp"

class Pipe
{
public:
    explicit Pipe(size_t capacity) : capacity_(capacity) {}

    // Producer writes data into the pipe
    bool write(const char *data, size_t len, std::atomic<bool> &stop)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        for (size_t i = 0; i < len; ++i)
        {
            if (stop.load())
            {
                return false; // Stop requested
            }

            while (queue_.size() >= capacity_ && !stop.load())
            {
                condNotFull_.wait(lock);
            }

            if (stop.load())
            {
                return false;
            }

            queue_.push(data[i]);
            condNotEmpty_.notify_one();
        }
        // Logger::Log(LogLevel::DEBUG, "Pipe::write: Attempting to write " + std::to_string(len) + " bytes. Current size: " + std::to_string(queue_.size()));

        return true;
    }

    // Consumer reads data from the pipe
    size_t read(char *dest, size_t len, std::atomic<bool> &stop)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t bytesRead = 0;

        while (bytesRead < len)
        {
            if (stop.load() && queue_.empty())
            {
                Logger::Log(LogLevel::DEBUG, "Pipe::read: Returning EOF. Pipe is empty and stop is requested.");
                break;
            }

            if (!queue_.empty())
            {
                dest[bytesRead++] = queue_.front();
                queue_.pop();
                condNotFull_.notify_one();
            }
            else
            {
                // Logger::Log(LogLevel::DEBUG, "Pipe::read: Pipe is empty. Waiting for data.");
                condNotEmpty_.wait(lock, [&]
                                   { return !queue_.empty() || stop.load(); });
            }
        }

        Logger::Log(LogLevel::DEBUG, "Pipe::read: Read " + std::to_string(bytesRead) + " bytes. Requested: " + std::to_string(len));
        if (bytesRead > 0)
        {
            // Logger::Log(LogLevel::DEBUG, "Pipe::read: Read " + std::to_string(bytesRead) + " bytes. Buffer filled partially/completely.");
        }
        else
        {
            Logger::Log(LogLevel::DEBUG, "Pipe::read: Returning 0 bytes. Pipe is empty.");
        }
        return bytesRead;
    }

private:
    std::queue<char> queue_;
    size_t capacity_;
    std::mutex mutex_;
    std::condition_variable condNotEmpty_;
    std::condition_variable condNotFull_;
};
