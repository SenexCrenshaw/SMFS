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
            stbuf->st_size = it->second->content ? it->second->content->size() : 1024 * 1024; // Default size
        }
        return 0;
    }

    return -ENOENT;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi, fuse_readdir_flags flags)
{
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Path: " + std::string(path));
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Total entries in g_state->files: " + std::to_string(g_state->files.size()));

    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));  // Current directory
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0)); // Parent directory

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    if (strcmp(path, "/") == 0) // Root directory
    {
        for (const auto &entry : g_state->files)
        {
            std::string relativePath = entry.first.substr(1); // Remove leading '/'
            if (relativePath.find("/") == std::string::npos)  // Direct child of root
            {
                filler(buf, relativePath.c_str(), NULL, 0, static_cast<fuse_fill_dir_flags>(0));
                Logger::Log(LogLevel::DEBUG, "fs_readdir: Added root entry: " + relativePath);
            }
        }
        return 0;
    }

    // Handle subdirectories
    std::string prefix = std::string(path) + "/";
    for (const auto &entry : g_state->files)
    {
        if (entry.first.find(prefix) == 0)
        {
            std::string relativePath = entry.first.substr(prefix.size());
            if (relativePath.find("/") == std::string::npos) // Direct child
            {
                filler(buf, relativePath.c_str(), NULL, 0, static_cast<fuse_fill_dir_flags>(0));
                Logger::Log(LogLevel::DEBUG, "fs_readdir: Added entry for path: " + relativePath);
            }
        }
    }
    return 0;
}

int fs_open(const char *path, fuse_file_info *fi)
{
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second != nullptr)
    {
        if (it->second->streamContext == nullptr)
        {
            it->second->streamContext = std::make_unique<StreamManager>(it->second->url, 50 * 1024 * 1024, g_state->isShuttingDown);
            it->second->streamContext->startStreaming();
        }

        Logger::Log(LogLevel::DEBUG, "fs_open: Incrementing reader count for path: " + std::string(path));
        it->second->streamContext->incrementReaderCount(); // Increment the reader count
        return 0;
    }

    Logger::Log(LogLevel::ERROR, "fs_open: Failed to find or initialize stream for path: " + std::string(path));
    return -ENOENT;
}

int fs_flush(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_flush: Path: " + std::string(path));
    return 0; // No action required
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_read: Attempting to read " + std::to_string(size) + " bytes from path: " + std::string(path) +
                                     ", Offset: " + std::to_string(offset));

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second != nullptr)
    {
        if (it->second->streamContext == nullptr)
        {
            it->second->streamContext = std::make_unique<StreamManager>(it->second->url, 50 * 1024 * 1024, g_state->isShuttingDown);
            it->second->streamContext->startStreaming();
        }

        StreamManager *streamManager = it->second->streamContext.get();
        if (!streamManager)
        {
            Logger::Log(LogLevel::ERROR, "fs_read: StreamManager not found for path: " + std::string(path));
            return -ENOENT;
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

int fs_opendir(const char *path, fuse_file_info *fi)
{
    if (strcmp(path, "/") == 0) // Root directory
    {
        Logger::Log(LogLevel::DEBUG, "fs_opendir: Opened root directory");
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second == nullptr) // Directory
    {
        Logger::Log(LogLevel::DEBUG, "fs_opendir: Opened directory: " + std::string(path));
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
