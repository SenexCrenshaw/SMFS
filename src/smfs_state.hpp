#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <atomic>
#include <vector>      // Required for std::vector
#include <curl/curl.h> // Required for CURL
#include "api_client.hpp"

struct VirtualFile
{
    std::string url;
    CURL *curl;
    std::vector<char> buffer;
    std::mutex bufferMutex;
    std::atomic<size_t> bufferSize{0};
};

struct SMFS
{
    std::map<std::string, std::shared_ptr<VirtualFile>> files;
    std::mutex filesMutex;
    APIClient apiClient;

    SMFS(const std::string &host, const std::string &port, const std::string &apiKey)
        : apiClient(host, port, apiKey) {}
};

extern SMFS *g_state;
