// File: file_operations.cpp
#include "file_operations.hpp"
#include <logger.hpp>
#include <fuse_operations.hpp>
#include <async_curl_client.hpp>
#include <unistd.h>
#include <string>
#include <iostream>
#include <smfs_state.hpp>

// Open callback
void fs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_open: Inode: " + std::to_string(ino) + ", Path: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);

        if (it != g_state->files.end() && it->second)
        {
            auto vf = it->second.get();

            // Handle .ts files
            if (path.ends_with(".ts"))
            {
                if (!vf->streamContext)
                {
                    Logger::Log(LogLevel::DEBUG, "fs_open: Creating StreamManager for .ts file: " + path);
                    try
                    {
                        // Create an AsyncCurlClient instance
                        std::shared_ptr<IStreamingClient> asyncClient = std::make_shared<AsyncCurlClient>();

                        // Create and configure StreamManager
                        vf->streamContext = std::make_unique<StreamManager>(vf->url, 4 * 1024 * 1024, asyncClient, g_state->isShuttingDown);

                        // Start streaming in a controlled thread
                        vf->streamContext->startStreamingThread();

                        Logger::Log(LogLevel::DEBUG, "fs_open: StreamManager successfully created and started for: " + path);
                    }
                    catch (const std::exception &e)
                    {
                        Logger::Log(LogLevel::ERROR, "fs_open: Failed to create StreamManager for path: " + path + ". Error: " + e.what());
                        vf->streamContext.reset(); // Ensure no dangling pointer
                        fuse_reply_err(req, ENOMEM);
                        return;
                    }
                }
                else
                {
                    Logger::Log(LogLevel::DEBUG, "fs_open: Reusing existing StreamManager for: " + path);
                }

                vf->streamContext->incrementReaderCount(); // Increment reader count
            }

            // Pass the VirtualFile pointer to file handle
            fi->fh = reinterpret_cast<uint64_t>(vf);
            fuse_reply_open(req, fi);
            return;
        }
    }

    Logger::Log(LogLevel::ERROR, "fs_open: File not found: " + path);
    fuse_reply_err(req, ENOENT);
}

// Write callback
void fs_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_write: Writing " + std::to_string(size) + " bytes to " + path);

    // Redirect writes for external files to cacheDir
    std::string fullPath = g_state->cacheDir + path;
    Logger::Log(LogLevel::DEBUG, "fs_write: Redirecting write to: " + fullPath);

    int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd == -1)
    {
        fuse_reply_err(req, errno);
        return;
    }

    ssize_t res = pwrite(fd, buf, size, off);
    close(fd);

    if (res == -1)
    {
        fuse_reply_err(req, errno);
    }
    else
    {
        fuse_reply_write(req, res);
    }
}

// Release callback
void fs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_release: Inode: " + std::to_string(ino) + ", Path: " + path);

    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second)
    {
        auto vf = it->second.get();
        if (vf->streamContext)
        {
            Logger::Log(LogLevel::DEBUG, "fs_release: Decrementing reader count for path: " + path);
            vf->streamContext->decrementReaderCount();

            if (vf->streamContext->isStopped())
            {
                Logger::Log(LogLevel::DEBUG, "fs_release: No more readers. Stopping stream: " + path);
                vf->streamContext->stopStreaming();
                vf->streamContext.reset(); // Release the StreamManager
            }
        }
    }

    Logger::Log(LogLevel::DEBUG, "fs_release: Inode: " + std::to_string(ino));
    fuse_reply_err(req, 0);
}

void fs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_read: Inode: " + std::to_string(ino) + ", Path: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);

        if (it != g_state->files.end() && it->second)
        {
            auto vf = it->second.get();
            StreamManager *streamManager = vf->streamContext.get();

            // Handle virtual files (.ts)
            if (path.ends_with(".ts"))
            {
                if (!streamManager)
                {
                    Logger::Log(LogLevel::ERROR, "fs_read: StreamManager not found for virtual file: " + path);
                    fuse_reply_err(req, ENOENT);
                    return;
                }

                char *buf = new char[size];
                size_t bytesRead = streamManager->getPipe().read(buf, size, g_state->isShuttingDown);

                Logger::Log(LogLevel::TRACE, "fs_read: Virtual file read returned " + std::to_string(bytesRead) + " bytes for path: " + path);
                fuse_reply_buf(req, buf, bytesRead);
                delete[] buf;
                return;
            }

            // Handle other virtual files (.strm, .xml, .m3u)
            if (path.ends_with(".strm"))
            {
                // Return the contentUrl as plain text
                std::string contentUrl = vf->url;
                Logger::Log(LogLevel::DEBUG, "fs_read: Returning contentUrl for .strm file: " + contentUrl);

                size_t toRead = std::min(size, contentUrl.size() - static_cast<size_t>(off));
                if (static_cast<size_t>(off) >= contentUrl.size())
                {
                    fuse_reply_buf(req, nullptr, 0); // EOF
                }
                else
                {
                    fuse_reply_buf(req, contentUrl.data() + off, toRead);
                }
                return;
            }

            if (path.ends_with(".xml") || path.ends_with(".m3u"))
            {
                std::string contentUrl = vf->url;
                if (path.ends_with(".xml"))
                    contentUrl += ".xml";
                else if (path.ends_with(".m3u"))
                    contentUrl += ".m3u";

                Logger::Log(LogLevel::DEBUG, "fs_read: Fetching content from URL: " + contentUrl);

                char *buf = new char[size];
                size_t bytesRead = streamManager->readContent(contentUrl, buf, size, off);
                fuse_reply_buf(req, buf, bytesRead);
                delete[] buf;
                return;
            }
        }
    }

    // Handle physical files in the cache directory
    std::string cachePath = g_state->cacheDir + path;
    Logger::Log(LogLevel::DEBUG, "fs_read: Falling back to cacheDir for file: " + cachePath);

    int fd = open(cachePath.c_str(), O_RDONLY);
    if (fd == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_read: File not found in cacheDir: " + cachePath);
        fuse_reply_err(req, ENOENT);
        return;
    }

    char *buf = new char[size];
    ssize_t res = pread(fd, buf, size, off);
    close(fd);

    if (res == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_read: Error reading file in cacheDir: " + cachePath);
        delete[] buf;
        fuse_reply_err(req, errno);
    }
    else
    {
        Logger::Log(LogLevel::DEBUG, "fs_read: Read " + std::to_string(res) + " bytes from cacheDir: " + cachePath);
        fuse_reply_buf(req, buf, res);
        delete[] buf;
    }
}