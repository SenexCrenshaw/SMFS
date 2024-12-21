#include "fuse_operations.hpp"
#include "logger.hpp"
#include "ring_buffer.hpp"
#include <algorithm>
#include <set>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <iostream>

// Utility to check file extensions
static bool endsWith(const std::string &s, const std::string &suffix)
{
    if (suffix.size() > s.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

// Write callback for curl_multi
static size_t multiWriteCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto ctx = reinterpret_cast<StreamContext *>(userdata);
    size_t total = size * nmemb;
    bool ok = ctx->ringBuf.push(ptr, total, ctx->stopRequested);
    return ok ? total : 0; // If 0, curl sees an error => abort
}

// Streaming thread for .ts files
static void streamingThreadFunc(StreamContext *ctx)
{
    Logger::Log(LogLevel::DEBUG, "streamingThreadFunc started for: " + ctx->url);

    auto doOneSession = [&](int attempt) -> CURLcode
    {
        ctx->curl = curl_easy_init();
        if (!ctx->curl)
        {
            Logger::Log(LogLevel::ERROR, "curl_easy_init failed!");
            return CURLE_FAILED_INIT;
        }

        curl_easy_setopt(ctx->curl, CURLOPT_URL, ctx->url.c_str());
        curl_easy_setopt(ctx->curl, CURLOPT_WRITEFUNCTION, multiWriteCallback);
        curl_easy_setopt(ctx->curl, CURLOPT_WRITEDATA, ctx);
        curl_easy_setopt(ctx->curl, CURLOPT_TIMEOUT, ctx->readTimeout);

        CURLMcode rc = curl_multi_add_handle(ctx->multi, ctx->curl);
        if (rc != CURLM_OK)
        {
            Logger::Log(LogLevel::ERROR, "curl_multi_add_handle failed!");
            curl_easy_cleanup(ctx->curl);
            ctx->curl = nullptr;
            return CURLE_FAILED_INIT;
        }

        int stillRunning = 0;
        do
        {
            if (ctx->stopRequested.load())
            {
                break;
            }

            CURLMcode mc = curl_multi_perform(ctx->multi, &stillRunning);
            if (mc != CURLM_OK)
            {
                Logger::Log(LogLevel::ERROR, "curl_multi_perform error: " + std::to_string(mc));
                break;
            }

            // Check if RingBuffer is too empty and back-pressure curl
            if (ctx->ringBuf.size() < SOME_THRESHOLD)
            {
                mc = curl_multi_poll(ctx->multi, nullptr, 0, 100, nullptr);
                if (mc != CURLM_OK)
                {
                    Logger::Log(LogLevel::ERROR, "curl_multi_poll error: " + std::to_string(mc));
                    break;
                }
            }

        } while (stillRunning);

        curl_multi_remove_handle(ctx->multi, ctx->curl);
        curl_easy_cleanup(ctx->curl);
        ctx->curl = nullptr;
        return CURLE_OK;
    };

    for (int attempt = 0; attempt <= ctx->maxRetries; ++attempt)
    {
        if (ctx->stopRequested.load())
            break;

        CURLcode ret = doOneSession(attempt);
        if (ret == CURLE_OK)
        {
            Logger::Log(LogLevel::INFO, "Streaming ended normally: " + ctx->url);
            break;
        }
        else if (ret == CURLE_ABORTED_BY_CALLBACK)
        {
            Logger::Log(LogLevel::INFO, "Streaming aborted: " + ctx->url);
            break;
        }
        else
        {
            Logger::Log(LogLevel::WARN, "Streaming retry " + std::to_string(attempt) + " for: " + ctx->url);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    ctx->stopRequested.store(true);
    ctx->ringBuf.clear();
    Logger::Log(LogLevel::DEBUG, "streamingThreadFunc ended for: " + ctx->url);
}

// ----------------------------------------------------------------------------
// FUSE callbacks
// ----------------------------------------------------------------------------

int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_getattr: ") + path);
    memset(stbuf, 0, sizeof(struct stat));

    if (std::string(path) == "/")
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it == g_state->files.end())
    {
        return -ENOENT;
    }

    if (!it->second)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
    }
    else
    {
        // It's a file
        stbuf->st_mode = S_IFREG | 0444; // Regular file, read-only
        stbuf->st_nlink = 1;

        // Simulate large size for .ts files
        if (endsWith(path, ".ts"))
        {
            stbuf->st_size = 1024L * 1024L * 1024L; // Simulate 1 GB size
            // Logger::Log(LogLevel::DEBUG, "fs_getattr: Simulating large file size for .ts file -> " + std::string(path));
        }
        else
        {
            stbuf->st_size = 1024; // Placeholder size for other files
        }
    }
    return 0;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi, fuse_readdir_flags flags)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_readdir: ") + path);

    filler(buf, ".", nullptr, 0, (fuse_fill_dir_flags)0);
    filler(buf, "..", nullptr, 0, (fuse_fill_dir_flags)0);

    std::string current(path ? path : "/");
    if (current.empty())
        current = "/";

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    if (current == "/")
    {
        for (auto &kv : g_state->files)
        {
            if (kv.first == "/")
                continue;
            if (kv.first.find('/', 1) == std::string::npos)
            {
                std::string name = kv.first.substr(1);
                filler(buf, name.c_str(), nullptr, 0, (fuse_fill_dir_flags)0);
            }
        }
    }
    else
    {
        if (current.back() != '/')
            current.push_back('/');

        std::set<std::string> childNames;
        for (auto &kv : g_state->files)
        {
            if (kv.first.rfind(current, 0) == 0 && kv.first != path)
            {
                std::string remainder = kv.first.substr(current.size());
                auto slashPos = remainder.find('/');
                if (slashPos == std::string::npos)
                {
                    childNames.insert(remainder);
                }
                else
                {
                    childNames.insert(remainder.substr(0, slashPos));
                }
            }
        }
        for (auto &nm : childNames)
        {
            filler(buf, nm.c_str(), nullptr, 0, (fuse_fill_dir_flags)0);
        }
    }
    return 0;
}

int fs_open(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_open: ") + path);

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it == g_state->files.end())
    {
        return -ENOENT;
    }

    if (!it->second)
    {
        return -EISDIR;
    }

    fi->fh = reinterpret_cast<uint64_t>(it->second.get());

    if (endsWith(path, ".ts"))
    {
        auto vf = it->second.get();
        if (!vf->streamContext)
        {
            vf->streamContext = std::make_unique<StreamContext>(vf->url, 1024 * 1024); // 1 MB buffer
            vf->streamContext->multi = curl_multi_init();

            vf->streamContext->stopRequested.store(false);
            vf->streamContext->ringBuf.clear();

            vf->streamContext->thread = std::thread(streamingThreadFunc, vf->streamContext.get());
        }

        vf->streamContext->activeReaders.fetch_add(1);
    }

    return 0;
}
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t totalSize = size * nmemb;
    std::string *str = static_cast<std::string *>(userp);
    str->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}
size_t readContentFromUrl(const std::string &url, char *buf, size_t size, size_t offset)
{
    CURL *curl;
    CURLcode res;
    std::string retrievedData;

    curl = curl_easy_init();
    if (curl)
    {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &retrievedData);

        // Perform the request
        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
        {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            curl_easy_cleanup(curl);
            return 0; // Indicate failure
        }

        curl_easy_cleanup(curl);

        // Ensure the offset is within bounds
        if (offset >= retrievedData.size())
            return 0;

        // Calculate the number of bytes to read
        size_t toRead = std::min(size, retrievedData.size() - offset);
        memcpy(buf, retrievedData.data() + offset, toRead);
        return toRead;
    }

    return 0; // Indicate failure if curl couldn't be initialized
}
int fs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi)
{
    // Logger::Log(LogLevel::DEBUG, std::string("fs_read: ") + path);

    auto vf = reinterpret_cast<VirtualFile *>(fi->fh);
    if (!vf)
        return -ENOENT;

    if (endsWith(path, ".strm"))
    {
        const auto &url = vf->url;
        if (static_cast<size_t>(offset) >= url.size())
            return 0; // EOF
        size_t toRead = std::min(size, url.size() - static_cast<size_t>(offset));
        memcpy(buf, url.data() + offset, toRead);
        return static_cast<int>(toRead);
    }
    else if (endsWith(path, ".xml"))
    {
        // return url + ".xml"
        std::string content = vf->url + ".xml";
        Logger::Log(LogLevel::DEBUG, std::string("fs_read: reading xml from ") + content);
        size_t bytesRead = readContentFromUrl(content, buf, size, offset);
        return bytesRead;
    }
    else if (endsWith(path, ".m3u"))
    {
        // return url + ".m3u"
        std::string content = vf->url + ".m3u";
        Logger::Log(LogLevel::DEBUG, std::string("fs_read: reading m3u from ") + content);
        size_t bytesRead = readContentFromUrl(content, buf, size, offset);
        return bytesRead;
    }
    else if (endsWith(path, ".ts"))
    {
        if (!vf->streamContext)
            return 0;

        size_t bytesRead = 0;
        while (bytesRead == 0 && !vf->streamContext->stopRequested)
        {
            bytesRead = vf->streamContext->ringBuf.pop(buf, size, vf->streamContext->stopRequested);
            if (bytesRead == 0)
            {
                // No data available, wait briefly and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        // Logger::Log(LogLevel::DEBUG, "fs_read: Read " + std::to_string(bytesRead) + " bytes from: " + path);
        return static_cast<int>(bytesRead);
    }

    return 0;
}

int fs_release(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_release: ") + path);

    auto vf = reinterpret_cast<VirtualFile *>(fi->fh);
    if (!vf)
        return 0;

    if (endsWith(path, ".ts") && vf->streamContext)
    {
        int activeReaders = vf->streamContext->activeReaders.fetch_sub(1) - 1;

        if (activeReaders <= 0)
        {
            Logger::Log(LogLevel::DEBUG, "No active readers. Delaying cleanup for: " + std::string(path));

            std::thread([vf]()
                        {
                std::this_thread::sleep_for(std::chrono::milliseconds(vf->streamContext->delayShutdownTime));
                if (vf->streamContext->activeReaders.load() == 0)
                {
                    vf->streamContext->stopRequested.store(true);
                    if (vf->streamContext->thread.joinable())
                    {
                        vf->streamContext->thread.join();
                    }
                    vf->streamContext->ringBuf.clear();
                    Logger::Log(LogLevel::INFO, "Streaming thread stopped after delay: " + vf->url);
                } })
                .detach();
        }
    }

    return 0;
}

int fs_releasedir(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_releasedir: ") + path);
    return 0;
}

int fs_create(const char *path, mode_t mode, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_create called for path: ") + path);

    // 1. Build a string for 'fullPath'
    std::string fullPath(path);

    // 2. Extract parent directory
    //    For example, if path = "/Test/HBO/folder.png",
    //    parentDir = "/Test/HBO"
    std::string parentDir = "/";
    {
        // Find the last slash that isn't the very first char
        size_t pos = fullPath.rfind('/');
        if (pos == std::string::npos || pos == 0)
        {
            // This means the file is directly under "/", e.g. "/folder.png"
            // so parentDir is "/"
        }
        else
        {
            parentDir = fullPath.substr(0, pos);
            if (parentDir.empty())
                parentDir = "/";
        }
    }

    // 3. Check that 'parentDir' exists and is a directory
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(parentDir);
        if (it == g_state->files.end())
        {
            // Parent doesn't exist
            Logger::Log(LogLevel::WARN, "fs_create: Parent directory not found -> " + parentDir);
            return -ENOENT;
        }
        if (it->second != nullptr)
        {
            // Means it's a file, not a directory
            Logger::Log(LogLevel::WARN, "fs_create: Parent is not a directory -> " + parentDir);
            return -ENOTDIR;
        }
    }

    // 4. Check if this file already exists
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(fullPath);
        if (it != g_state->files.end())
        {
            // Already exists
            Logger::Log(LogLevel::WARN, "fs_create: File already exists -> " + fullPath);
            return -EEXIST;
        }
    }

    // 5. Create a new VirtualFile for user data
    auto vf = std::make_shared<VirtualFile>();
    vf->isUserFile = true;
    vf->st_mode = 0777;                                  // capture the mode user requested
    vf->content = std::make_shared<std::vector<char>>(); // store data in memory

    // 6. Insert into g_state->files map
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        g_state->files[fullPath] = vf;
    }

    // 7. Let fuse know it's open
    fi->fh = reinterpret_cast<uint64_t>(vf.get());
    fi->direct_io = 1; // or 0, depends if you want direct I/O

    Logger::Log(LogLevel::DEBUG, "fs_create: File created -> " + fullPath);
    return 0;
}

int fs_write(const char *path, const char *buf, size_t size,
             off_t offset, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_write: ") + path);

    auto vf = reinterpret_cast<VirtualFile *>(fi->fh);
    if (!vf)
        return -ENOENT;

    if (!vf->isUserFile)
    {
        // This is not a user-created file -> read-only
        Logger::Log(LogLevel::WARN, "fs_write: Attempt to write read-only file -> " + std::string(path));
        return -EACCES;
    }

    if (!vf->content)
    {
        // No buffer allocated?
        return -EIO;
    }

    // Ensure vector is big enough
    size_t newSize = offset + size;
    if (newSize > vf->content->size())
    {
        vf->content->resize(newSize);
    }

    // Copy data
    memcpy(vf->content->data() + offset, buf, size);

    return static_cast<int>(size);
}

int fs_chmod(const char *path, mode_t mode, fuse_file_info *fi)
{
    // (void)fi; // optionally ignore fi if you want
    Logger::Log(LogLevel::DEBUG, std::string("fs_chmod: ") + path + " mode=" + std::to_string(mode));

    std::string p(path);

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(p);
    if (it == g_state->files.end() || !it->second)
    {
        // Not found or directory
        return -ENOENT;
    }

    auto vf = it->second.get(); // pointer to VirtualFile
    if (!vf->isUserFile)
    {
        // If it's one of your .ts/.strm/.xml/.m3u read-only files
        return -EACCES;
    }

    // Update mode bits
    vf->st_mode = (mode & 07777); // keep only permission bits
    return 0;
}

int fs_opendir(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, std::string("fs_opendir: ") + path);

    if (std::string(path) == "/")
    {
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it == g_state->files.end())
    {
        return -ENOENT;
    }

    if (it->second)
    {
        return -ENOTDIR;
    }

    return 0;
}