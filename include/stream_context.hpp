// stream_context.hpp
#pragma once
#include "ring_buffer.hpp"
#include <atomic>
#include <string>
#include <thread>
#include <curl/curl.h>

// A ring-buffer-based struct
struct StreamContext
{
    CURLM *multi = nullptr;
    CURL *curl = nullptr;
    std::thread thread;
    std::atomic<bool> stopRequested{false};
    std::atomic<int> activeReaders{0}; // Track active readers

    RingBuffer ringBuf;
    CURLcode lastCode = CURLE_OK;

    std::string url;
    int readTimeout = 30;
    int maxRetries = 1;
    int delayShutdownTime = 1000;
    int maxWaitTime = 10000; // Max wait time for data in milliseconds
    std::atomic<int> currentRetry{0};

    StreamContext(const std::string &u, size_t cap)
        : ringBuf(cap), url(u)
    {
    }
};
