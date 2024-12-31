#pragma once

#include <map>
#include <mutex>
#include <memory>
#include "api_client.hpp"
#include <atomic>
#include <thread>
#include <curl/curl.h>
#include <deque>
#include <condition_variable>
#include <set>
#include "stream_manager.hpp"

// Forward declare so we can reference below
struct StreamContext;

// The "file" in FUSE
struct VirtualFile
{
    std::string url;

    // A StreamManager pointer for indefinite streaming
    std::unique_ptr<StreamManager> streamContext;

    bool isUserFile = false;
    std::shared_ptr<std::vector<char>> content;
    mode_t st_mode = 0111; // default
    uid_t st_uid = 0;      // optional
    gid_t st_gid = 0;      // optional

    VirtualFile() = default;
    explicit VirtualFile(const std::string &u)
        : url(u) {}

    // No copy
    VirtualFile(const VirtualFile &) = delete;
    VirtualFile &operator=(const VirtualFile &) = delete;

    // Move
    VirtualFile(VirtualFile &&) = default;
    VirtualFile &operator=(VirtualFile &&) = default;
};

// SMFS = "Stream Master File System"
struct SMFS
{
    std::atomic<bool> isShuttingDown{false};
    std::set<std::string> enabledFileTypes;
    std::string storageDir;
    // Map of path -> VirtualFile (or nullptr if directory)
    std::map<std::string, std::shared_ptr<VirtualFile>> files;
    std::mutex filesMutex;

    APIClient apiClient;

    SMFS(const std::string &host,
         const std::string &port,
         const std::string &apiKey,
         const std::string &streamGroupProfileIds = "0",
         bool isShort = true)
        : apiClient(host, port, apiKey, streamGroupProfileIds, isShort)
    {
    }

    // Non-copyable
    SMFS(const SMFS &) = delete;
    SMFS &operator=(const SMFS &) = delete;
};

extern SMFS *g_state;
