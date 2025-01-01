#pragma once
#include "pipe.hpp" // Ensure Pipe is correctly included
#include <thread>
#include <atomic>
#include <string>
#include <curl/curl.h>
#include "logger.hpp"
#include <cstring>
class StreamManager
{
public:
    explicit StreamManager(const std::string &url, size_t bufferCapacity, std::atomic<bool> &shutdownFlag)
        : url_(url), pipe_(bufferCapacity), stopRequested_(false), isShuttingDown_(shutdownFlag) {}

    void incrementReaderCount()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++readerCount_;
        stopRequested_ = false; // Ensure the stream is not stopped
        Logger::Log(LogLevel::DEBUG, "StreamManager::incrementReaderCount: Reader count increased to " + std::to_string(readerCount_));
    }

    void decrementReaderCount()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (--readerCount_ <= 0)
        {
            Logger::Log(LogLevel::DEBUG, "StreamManager::decrementReaderCount: No readers left, stopping stream.");
            stopStreaming();
        }
    }

    std::atomic<bool> &getStopRequestedAtomic() { return stopRequested_; }
    void startStreaming()
    {
        stopRequested_ = false;
        streamingThread_ = std::thread(&StreamManager::streamingThreadFunc, this);
    }

    void stopStreaming()
    {
        stopRequested_ = true;
        if (streamingThread_.joinable())
        {
            streamingThread_.join();
        }
    }

    Pipe &getPipe() { return pipe_; }
    bool isStopped() const { return stopRequested_; }

    size_t readContent(const std::string &toFetchUrl, char *buf, size_t size, off_t offset)
    {
        return fetchUrlContent(toFetchUrl, buf, size, offset);
    }

private:
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
    {
        size_t totalSize = size * nmemb;
        std::string *str = static_cast<std::string *>(userp);
        str->append(static_cast<char *>(contents), totalSize);
        return totalSize;
    }

    size_t fetchUrlContent(const std::string &url, char *buf, size_t size, off_t offset)
    {
        CURL *curl = curl_easy_init();
        if (!curl)
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::fetchUrlContent: Failed to initialize CURL.");
            return 0;
        }

        CURLcode res;
        std::string retrievedData;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &retrievedData);

        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::fetchUrlContent: CURL error: " + std::string(curl_easy_strerror(res)));
            return 0;
        }

        if (offset >= retrievedData.size())
        {
            return 0; // EOF
        }

        size_t toRead = std::min(size, retrievedData.size() - offset);
        memcpy(buf, retrievedData.data() + offset, toRead);
        return toRead;
    }

    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
    {
        auto *manager = reinterpret_cast<StreamManager *>(userdata);
        size_t total = size * nmemb;

        if (manager->stopRequested_)
        {
            Logger::Log(LogLevel::DEBUG, "StreamManager::writeCallback: Stop requested, exiting write.");
            return 0; // Inform CURL to abort
        }

        if (!manager->pipe_.write(ptr, total, manager->stopRequested_))
        {
            Logger::Log(LogLevel::DEBUG, "StreamManager::writeCallback: Failed to write, exiting.");
            return 0; // Exit if unable to write
        }

        return total;
    }

    void streamingThreadFunc()
    {
        Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Starting stream for URL: " + url_);

        CURL *curl = curl_easy_init();
        if (!curl)
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::streamingThreadFunc: Failed to initialize CURL.");
            return;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 100000L);
        curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

        while (!stopRequested_ && !isShuttingDown_.load())
        {
            Logger::Log(LogLevel::DEBUG, "StreamManager::streamingThreadFunc: Attempting stream for URL: " + url_);
            CURLcode res = curl_easy_perform(curl);

            if (res != CURLE_OK)
            {
                Logger::Log(LogLevel::ERROR, "StreamManager::streamingThreadFunc: CURL error: " + std::string(curl_easy_strerror(res)));
                std::this_thread::sleep_for(std::chrono::seconds(5)); // Retry after delay
            }
            else
            {
                Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Stream completed successfully for URL: " + url_);
                break;
            }
        }

        curl_easy_cleanup(curl);
        Logger::Log(LogLevel::DEBUG, "StreamManager::streamingThreadFunc: Loop exit condition met for URL: " + url_ +
                                         ", stopRequested_=" + std::to_string(stopRequested_) +
                                         ", isShuttingDown_=" + std::to_string(isShuttingDown_.load()));

        Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Exiting for URL: " + url_);
    }

    std::atomic<int> readerCount_{0};
    std::string url_;
    Pipe pipe_;
    std::thread streamingThread_;
    std::atomic<bool> stopRequested_;
    std::mutex mutex_;
    std::atomic<bool> &isShuttingDown_; // Reference to the SMFS shutdown flag
};
