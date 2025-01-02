// File: smfs_state.hpp
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
    mode_t st_mode = 0111; // default
    uid_t st_uid = 0;      // optional
    gid_t st_gid = 0;      // optional
    std::shared_ptr<std::vector<char>> content;

    // Default constructor
    VirtualFile() = default;

    // Constructor for URL only
    explicit VirtualFile(const std::string &u)
        : url(u) {}

    // Constructor for URL and other attributes
    VirtualFile(const std::string &u, mode_t mode, uid_t uid, gid_t gid, bool isUser = false)
        : url(u), isUserFile(isUser), st_mode(mode), st_uid(uid), st_gid(gid) {}

    // Constructor for URL and size
    VirtualFile(const std::string &u, long int size)
        : url(u)
    {
        // Initialize `content` if size > 0
        if (size > 0)
        {
            content = std::make_shared<std::vector<char>>(size);
        }
    }

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
    std::string cacheDir;
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

extern std::unique_ptr<SMFS> g_state;
