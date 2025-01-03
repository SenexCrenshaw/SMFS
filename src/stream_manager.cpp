// File: stream_manager.cpp
#include "stream_manager.hpp"
#include "logger.hpp"
#include <curl/curl.h>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <stop_token>
#include <future>

StreamManager::StreamManager(const std::string &url, size_t bufferCapacity, std::shared_ptr<IStreamingClient> client, std::atomic<bool> &shutdownFlag)
    : url_(url), pipe_(bufferCapacity), client_(std::move(client)), isShuttingDown_(shutdownFlag) {}

void StreamManager::incrementReaderCount()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++readerCount_;
    stopRequested_ = false;
    Logger::Log(LogLevel::DEBUG, "StreamManager::incrementReaderCount: Reader count increased to " + std::to_string(readerCount_));
}

void StreamManager::decrementReaderCount()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (--readerCount_ <= 0)
    {
        Logger::Log(LogLevel::DEBUG, "StreamManager::decrementReaderCount: No readers left, stopping stream.");
        stopStreaming();
    }
}

void StreamManager::startStreaming()
{
    Logger::Log(LogLevel::INFO, "StreamManager::startStreaming: Starting stream for URL: " + url_);
    client_->fetchStreamAsync(url_, [this](const std::string &data)
                              {
        if (!pipe_.write(data.data(), data.size(), stopRequested_))
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::startStreaming: Failed to write data to pipe.");
        } });
}

void StreamManager::stopStreaming()
{
    Logger::Log(LogLevel::INFO, "StreamManager::stopStreaming: Stopping stream for URL: " + url_);
    stopRequested_ = true;
}

void StreamManager::startStreamingThread()
{
    streamingThread_ = std::jthread([this](std::stop_token stopToken)
                                    {
        (void)stopToken;
        
        try
        {
            streamingThreadFunc();
        }
        catch (const std::exception &e)
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::startStreamingThread: Exception occurred: " + std::string(e.what()));
        } });
}

void StreamManager::stopStreamingThread()
{
    if (streamingThread_.joinable())
    {
        Logger::Log(LogLevel::INFO, "StreamManager::stopStreamingThread: Requesting thread stop for URL: " + url_);
        streamingThread_.request_stop();
        streamingThread_.join();
    }
}

const std::string &StreamManager::getUrl() const
{
    return url_;
}

Pipe &StreamManager::getPipe()
{
    return pipe_;
}

bool StreamManager::isStopped() const
{
    return stopRequested_;
}

size_t StreamManager::readContent(const std::string &toFetchUrl, char *buf, size_t size, off_t offset)
{
    return fetchUrlContent(toFetchUrl, buf, size, offset);
}

StreamManager::~StreamManager()
{
    Logger::Log(LogLevel::INFO, "StreamManager::~StreamManager: Cleaning up StreamManager for URL: " + url_);
    stopStreamingThread();
}

size_t StreamManager::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t totalSize = size * nmemb;
    std::string *str = static_cast<std::string *>(userp);
    str->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}

size_t StreamManager::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *manager = reinterpret_cast<StreamManager *>(userdata);
    size_t total = size * nmemb;

    if (manager->stopRequested_)
    {
        Logger::Log(LogLevel::INFO, "StreamManager::writeCallback: Stop requested. Exiting write.");
        return 0; // Inform CURL to stop
    }

    if (!manager->pipe_.write(ptr, total, manager->stopRequested_))
    {
        if (manager->stopRequested_)
        {
            Logger::Log(LogLevel::INFO, "StreamManager::writeCallback: Write aborted due to stop request.");
            return 0; // Stop without logging an error
        }

        Logger::Log(LogLevel::ERROR, "StreamManager::writeCallback: Failed to write to pipe.");
        return 0; // Inform CURL of failure
    }

    Logger::Log(LogLevel::DEBUG, "StreamManager::writeCallback: Wrote " + std::to_string(total) + " bytes to pipe.");
    return total;
}

size_t StreamManager::fetchUrlContent(const std::string &url, char *buf, size_t size, off_t offset)
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
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &retrievedData);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        Logger::Log(LogLevel::ERROR, "StreamManager::fetchUrlContent: CURL error: " + std::string(curl_easy_strerror(res)));
        return 0;
    }

    if (static_cast<std::make_unsigned_t<off_t>>(offset) >= retrievedData.size())
    {
        return 0;
    }

    size_t toRead = std::min(size, retrievedData.size() - offset);
    memcpy(buf, retrievedData.data() + offset, toRead);
    return toRead;
}

void StreamManager::streamingThreadFunc()
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

        if (res == CURLE_ABORTED_BY_CALLBACK && stopRequested_)
        {
            Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Stream stopped by request for URL: " + url_);
            break;
        }
        else if (res != CURLE_OK)
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::streamingThreadFunc: CURL error: " + std::string(curl_easy_strerror(res)));
            if (isShuttingDown_.load())
            {
                Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Exiting due to shutdown for URL: " + url_);
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        else
        {
            Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Stream completed successfully for URL: " + url_);
            break;
        }
    }

    curl_easy_cleanup(curl);
    Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Exiting for URL: " + url_);
}
