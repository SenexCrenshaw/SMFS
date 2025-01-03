// File: async_curl_client.hpp
#pragma once

#include "i_streaming_client.hpp"
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>

class AsyncCurlClient : public IStreamingClient
{
public:
    AsyncCurlClient();
    ~AsyncCurlClient();

    void fetchStreamAsync(const std::string &url, std::function<void(const std::string &data)> onDataReceived) override;

private:
    CURLM *multiHandle_;
    std::thread workerThread_;
    std::atomic<bool> isRunning_{true};
    std::map<CURL *, std::function<void()>> callbacks_;
    std::mutex mutex_;

    static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp);
    void eventLoop();
};
