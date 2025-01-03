// File: async_curl_client.cpp
#include "async_curl_client.hpp"
#include "logger.hpp"

AsyncCurlClient::AsyncCurlClient()
{
    curl_global_init(CURL_GLOBAL_ALL);
    multiHandle_ = curl_multi_init();
    workerThread_ = std::thread(&AsyncCurlClient::eventLoop, this);
}

AsyncCurlClient::~AsyncCurlClient()
{
    isRunning_ = false;
    if (workerThread_.joinable())
    {
        workerThread_.join();
    }
    curl_multi_cleanup(multiHandle_);
    curl_global_cleanup();
}

void AsyncCurlClient::fetchStreamAsync(const std::string &url, std::function<void(const std::string &data)> onDataReceived)
{
    CURL *easyHandle = curl_easy_init();
    if (!easyHandle)
    {
        throw std::runtime_error("Failed to initialize CURL easy handle");
    }

    std::string *responseData = new std::string();
    curl_easy_setopt(easyHandle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easyHandle, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(easyHandle, CURLOPT_WRITEDATA, responseData);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[easyHandle] = [onDataReceived, responseData]()
        {
            onDataReceived(*responseData);
            delete responseData;
        };
    }

    curl_multi_add_handle(multiHandle_, easyHandle);
}

size_t AsyncCurlClient::writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    auto *data = static_cast<std::string *>(userp);
    data->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

void AsyncCurlClient::eventLoop()
{
    while (isRunning_)
    {
        int runningHandles;
        curl_multi_perform(multiHandle_, &runningHandles);

        int numMessages;
        CURLMsg *msg;
        while ((msg = curl_multi_info_read(multiHandle_, &numMessages)))
        {
            if (msg->msg == CURLMSG_DONE)
            {
                CURL *easyHandle = msg->easy_handle;

                std::function<void()> callback;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = callbacks_.find(easyHandle);
                    if (it != callbacks_.end())
                    {
                        callback = std::move(it->second);
                        callbacks_.erase(it);
                    }
                }

                if (callback)
                {
                    callback();
                }

                curl_multi_remove_handle(multiHandle_, easyHandle);
                curl_easy_cleanup(easyHandle);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Polling interval
    }
}
