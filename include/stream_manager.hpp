// File: stream_manager.hpp
#pragma once
#include "pipe.hpp"
#include "i_streaming_client.hpp"
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include <memory>

class StreamManager
{
public:
    explicit StreamManager(const std::string &url, size_t bufferCapacity, std::shared_ptr<IStreamingClient> client, std::atomic<bool> &shutdownFlag);

    void incrementReaderCount();
    void decrementReaderCount();

    void startStreaming();
    void stopStreaming();
    void startStreamingThread();
    void stopStreamingThread();

    const std::string &getUrl() const;
    Pipe &getPipe();
    bool isStopped() const;

    size_t readContent(const std::string &toFetchUrl, char *buf, size_t size, off_t offset);

    ~StreamManager();

private:
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

    size_t fetchUrlContent(const std::string &url, char *buf, size_t size, off_t offset);
    void streamingThreadFunc();

    std::string url_;
    Pipe pipe_;
    std::shared_ptr<IStreamingClient> client_;
    std::jthread streamingThread_;
    std::atomic<int> readerCount_{0};
    std::atomic<bool> stopRequested_{false};
    std::mutex mutex_;
    std::atomic<bool> &isShuttingDown_;
};
