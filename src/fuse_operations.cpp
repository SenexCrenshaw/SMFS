#include "fuse_operations.hpp"
#include "logger.hpp"
#include <algorithm>
#include <set>
#include <chrono>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "stream_manager.hpp"
#include "utils.hpp"

int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_getattr: Path: " + std::string(path));
    memset(stbuf, 0, sizeof(struct stat));

    if (strcmp(path, "/") == 0) // Root directory
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end())
    {
        if (it->second == nullptr) // Directory
        {
            stbuf->st_mode = S_IFDIR | 0755;
            stbuf->st_nlink = 2;
        }
        else // File
        {
            stbuf->st_mode = S_IFREG | 0444;
            stbuf->st_nlink = 1;

            // Set st_size to INT64_MAX for streaming files
            stbuf->st_size = INT64_MAX;
        }
        return 0;
    }

    return -ENOENT;
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
                // Filter by file type
                std::string extension = name.substr(name.find_last_of('.') + 1);
                if (g_state->enabledFileTypes.find(extension) != g_state->enabledFileTypes.end() || kv.second == nullptr)
                {
                    filler(buf, name.c_str(), nullptr, 0, (fuse_fill_dir_flags)0);
                }
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
                    // Get file extension
                    std::string extension = remainder.substr(remainder.find_last_of('.') + 1);

                    // Filter by file type
                    if (g_state->enabledFileTypes.find(extension) != g_state->enabledFileTypes.end())
                    {
                        childNames.insert(remainder);
                    }
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

int fs_readdir2(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi, fuse_readdir_flags flags)
{
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Path: " + std::string(path));

    // Add current and parent directories
    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));  // Current directory
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0)); // Parent directory

    std::lock_guard<std::mutex> lock(g_state->filesMutex);

    std::string prefix = std::string(path) + (path[strlen(path) - 1] == '/' ? "" : "/");

    for (const auto &entry : g_state->files)
    {
        // Check if the entry is a direct child of the current directory
        if (entry.first.find(prefix) == 0)
        {
            std::string relativePath = entry.first.substr(prefix.size());
            if (relativePath.find("/") == std::string::npos) // Direct child only
            {
                if (entry.second == nullptr) // Directory
                {
                    struct stat stbuf = {};
                    stbuf.st_mode = S_IFDIR | 0755; // Directory permissions
                    stbuf.st_nlink = 2;             // Default link count
                    filler(buf, relativePath.c_str(), &stbuf, 0, static_cast<fuse_fill_dir_flags>(0));
                }
                else // File
                {

                    struct stat stbuf = {};
                    stbuf.st_mode = S_IFREG | 0444; // File permissions
                    stbuf.st_nlink = 1;             // Default link count
                    filler(buf, relativePath.c_str(), &stbuf, 0, static_cast<fuse_fill_dir_flags>(0));
                }

                Logger::Log(LogLevel::DEBUG, "fs_readdir: Added entry for path: " + relativePath);
            }
        }
    }

    return 0;
}

int fs_flush(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_flush: Path: " + std::string(path));
    return 0; // No action required
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_read: Attempting to read " + std::to_string(size) +
                                     " bytes from path: " + std::string(path) +
                                     ", Offset: " + std::to_string(offset));

    // Lock the file state
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);

    if (it != g_state->files.end() && it->second != nullptr)
    {
        auto vf = it->second.get();
        StreamManager *streamManager = vf->streamContext.get();
        if (!streamManager)
        {
            Logger::Log(LogLevel::ERROR, "fs_read: StreamManager not found for path: " + std::string(path));
            return -ENOENT;
        }

        if (ends_with(path, ".strm"))
        {
            const auto &url = vf->url;
            if (static_cast<size_t>(offset) >= url.size())
                return 0; // EOF
            size_t toRead = std::min(size, url.size() - static_cast<size_t>(offset));
            memcpy(buf, url.data() + offset, toRead);
            return static_cast<int>(toRead);
        }
        else if (ends_with(path, ".xml"))
        {
            std::string contentUrl = vf->url + ".xml";
            Logger::Log(LogLevel::DEBUG, "fs_read: Fetching XML content from: " + contentUrl);
            size_t bytesRead = streamManager->readContent(contentUrl, buf, size, offset);
            return static_cast<int>(bytesRead);
        }
        else if (ends_with(path, ".m3u"))
        {
            std::string contentUrl = vf->url + ".m3u";
            Logger::Log(LogLevel::DEBUG, "fs_read: Fetching M3U content from: " + contentUrl);
            size_t bytesRead = streamManager->readContent(contentUrl, buf, size, offset);
            return static_cast<int>(bytesRead);
        }
        else if (ends_with(path, ".ts"))
        {
            // Ensure the stream context exists
            if (vf->streamContext == nullptr)
            {
                vf->streamContext = std::make_unique<StreamManager>(vf->url, 50 * 1024 * 1024, g_state->isShuttingDown);
                vf->streamContext->startStreaming();
            }

            size_t bytesRead = 0;
            std::atomic<bool> &isStoppedAtomic = streamManager->getStopRequestedAtomic();

            while (bytesRead < size)
            {
                size_t chunkRead = streamManager->getPipe().read(buf + bytesRead, size - bytesRead, isStoppedAtomic);

                if (chunkRead == 0)
                {
                    if (isStoppedAtomic.load())
                    {
                        Logger::Log(LogLevel::DEBUG, "fs_read: Returning EOF. Reason: stopRequested_ is true.");
                        return 0;
                    }

                    if (g_state->isShuttingDown.load())
                    {
                        Logger::Log(LogLevel::DEBUG, "fs_read: Returning EOF. Reason: g_state->isShuttingDown is true.");
                        return 0;
                    }

                    Logger::Log(LogLevel::DEBUG, "fs_read: No data available. Retrying.");
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                else
                {
                    bytesRead += chunkRead;
                }
            }

            Logger::Log(LogLevel::DEBUG, "fs_read: Returning " + std::to_string(bytesRead) + " bytes.");
            return bytesRead;
        }

        Logger::Log(LogLevel::ERROR, "fs_read: Unsupported file type for path: " + std::string(path));
        return -EINVAL; // Unsupported file type
    }

    Logger::Log(LogLevel::ERROR, "fs_read: Path not found: " + std::string(path));
    return -ENOENT;
}

int fs_write(const char *path, const char *buf, size_t size, off_t offset, fuse_file_info *fi)
{
    std::string fullPath = g_state->storageDir + path;
    int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd == -1)
    {
        return -errno;
    }

    ssize_t res = pwrite(fd, buf, size, offset);
    close(fd);

    if (res == -1)
    {
        return -errno;
    }
    return res;
}

int fs_create(const char *path, mode_t mode, fuse_file_info *fi)
{
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    g_state->files[path] = std::make_shared<VirtualFile>();
    return 0;
}

int fs_opendir(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_opendir: Path: " + std::string(path ? path : "/"));

    std::string currentPath = path ? path : "/";
    if (currentPath.empty())
        currentPath = "/";

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    const auto &files = g_state->files;

    // Root directory case
    if (currentPath == "/")
    {
        Logger::Log(LogLevel::DEBUG, "fs_opendir: Root directory opened.");
        return 0; // Root directory always exists
    }

    // Ensure the directory path ends with '/'
    if (currentPath.back() != '/')
        currentPath.push_back('/');

    // Check if the directory exists
    bool directoryExists = false;
    for (const auto &kv : files)
    {
        if (kv.first.rfind(currentPath, 0) == 0) // Matches the directory
        {
            directoryExists = true;
            break;
        }
    }

    if (!directoryExists)
    {
        Logger::Log(LogLevel::ERROR, "fs_opendir: Directory not found: " + currentPath);
        return -ENOENT;
    }

    Logger::Log(LogLevel::DEBUG, "fs_opendir: Directory opened successfully: " + currentPath);
    return 0;
}

int fs_chmod(const char *path, mode_t mode, fuse_file_info *fi)
{
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second != nullptr)
    {
        it->second->st_mode = mode;
        return 0;
    }

    return -ENOENT;
}

int fs_open(const char *path, fuse_file_info *fi)
{
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second != nullptr)
    {
        if (it->second->streamContext == nullptr || it->second->streamContext->isStopped())
        {
            it->second->streamContext = std::make_unique<StreamManager>(it->second->url, 50 * 1024 * 1024, g_state->isShuttingDown);
            it->second->streamContext->startStreaming();
        }

        it->second->streamContext->incrementReaderCount();
        Logger::Log(LogLevel::DEBUG, "fs_open: Incrementing reader count for path: " + std::string(path));
        return 0;
    }

    return -ENOENT;
}

int fs_releasedir(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_releasedir: Releasing directory at path: " + std::string(path));
    return 0;
}

int fs_release(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_release: Releasing file at path: " + std::string(path) +
                                     ", File handle: " + std::to_string(fi->fh));

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second != nullptr && it->second->streamContext != nullptr)
    {
        Logger::Log(LogLevel::DEBUG, "fs_release: Decrementing reader count for path: " + std::string(path));
        it->second->streamContext->decrementReaderCount(); // Decrement the reader count
    }

    return 0; // Indicate successful release
}
