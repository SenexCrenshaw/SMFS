// fuse_operations.cpp
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
#include <sstream>
#include <iomanip>

// Map for path-to-inode mapping
std::unordered_map<std::string, fuse_ino_t> pathToInode;
std::unordered_map<fuse_ino_t, std::string> inodeToPath;
std::atomic<fuse_ino_t> nextInode{2}; // Start at 2, as 1 is reserved for the root inode

// Helper: Generate unique inodes
fuse_ino_t getInode(const std::string &path)
{
    Logger::Log(LogLevel::DEBUG, "getInode: Looking up inode for path: " + path);

    if (pathToInode.find(path) == pathToInode.end())
    {
        Logger::Log(LogLevel::TRACE, "getInode: Inode not found, creating new inode for path: " + path);
        pathToInode[path] = nextInode++;
        inodeToPath[pathToInode[path]] = path;
        Logger::Log(LogLevel::DEBUG, "getInode: Created inode " + std::to_string(pathToInode[path]) + " for path: " + path);
    }
    else
    {
        Logger::Log(LogLevel::TRACE, "getInode: Found existing inode " + std::to_string(pathToInode[path]) + " for path: " + path);
    }

    return pathToInode[path];
}

// Lookup callback
void fs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    std::string parentPath = inodeToPath[parent];
    std::string path = parentPath + "/" + name;

    // Normalize path (e.g., remove redundant slashes)
    while (path.find("//") != std::string::npos)
    {
        Logger::Log(LogLevel::TRACE, "fs_lookup: Found redundant slashes in path: " + path);
        path = path.replace(path.find("//"), 2, "/");
    }

    Logger::Log(LogLevel::DEBUG, "fs_lookup: Resolving path: " + path);

    struct fuse_entry_param e = {};
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);
        if (it != g_state->files.end())
        {
            e.ino = getInode(path);
            e.attr.st_ino = e.ino;
            e.attr.st_mode = it->second ? S_IFREG | 0444 : S_IFDIR | 0755;
            e.attr.st_nlink = it->second ? 1 : 2;
            e.attr.st_size = it->second ? INT64_MAX : 0;

            Logger::Log(LogLevel::TRACE, "fs_lookup: Resolved inode attributes for path: " + path);
            fuse_reply_entry(req, &e);
            return;
        }
    }

    Logger::Log(LogLevel::ERROR, "fs_lookup: Path not found: " + path);
    fuse_reply_err(req, ENOENT);
}

// Getattr callback
void fs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_getattr: Inode: " + std::to_string(ino));

    struct stat st = {};
    if (ino == FUSE_ROOT_ID)
    {
        st.st_ino = FUSE_ROOT_ID;
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 2;
        Logger::Log(LogLevel::DEBUG, "fs_getattr: Returning attributes for root directory.");
        fuse_reply_attr(req, &st, 1.0);
        return;
    }

    std::string path = inodeToPath[ino];
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);
        if (it != g_state->files.end())
        {
            st.st_ino = ino;
            st.st_mode = it->second ? S_IFREG | 0444 : S_IFDIR | 0755;
            st.st_nlink = it->second ? 1 : 2;
            st.st_size = it->second ? INT64_MAX : 0;
            Logger::Log(LogLevel::DEBUG, "fs_getattr: Returning attributes for path: " + path);
            fuse_reply_attr(req, &st, 1.0);
            return;
        }
    }

    Logger::Log(LogLevel::ERROR, "fs_getattr: Inode not found: " + std::to_string(ino));
    fuse_reply_err(req, ENOENT);
}

void logRawBuffer(const char *buf, size_t size)
{
    std::ostringstream oss;
    for (size_t i = 0; i < size; ++i)
    {
        oss << std::hex << std::setfill('0') << std::setw(2) << (unsigned int)(unsigned char)buf[i] << " ";
        if ((i + 1) % 16 == 0)
            oss << "\n";
    }
    Logger::Log(LogLevel::DEBUG, "Buffer (raw):\n" + oss.str());
}

// Readdir callback
void fs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Inode: " + std::to_string(ino));
    Logger::Log(LogLevel::TRACE, "fs_readdir: Offset: " + std::to_string(off));

    if (off > 0)
    {
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Offset > 0. No more entries to return.");
        fuse_reply_buf(req, nullptr, 0); // Signal EOF
        return;
    }

    if (inodeToPath.find(ino) == inodeToPath.end())
    {
        Logger::Log(LogLevel::ERROR, "fs_readdir: Inode not found: " + std::to_string(ino));
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string parentPath = inodeToPath[ino];
    if (parentPath.empty())
    {
        parentPath = "/";
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Parent Path was empty. Assuming root: /");
    }
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Resolved Parent Path: " + parentPath);

    char *buf = (char *)calloc(1, size); // Ensure buffer is zero-initialized
    size_t bufSize = 0;

    auto addDirEntry = [&](const char *name, fuse_ino_t inode, mode_t mode)
    {
        Logger::Log(LogLevel::TRACE, "fs_readdir: Adding entry: " + std::string(name));
        struct stat st = {};
        st.st_ino = inode;
        st.st_mode = mode;

        size_t entrySize = fuse_add_direntry(req, buf + bufSize, size - bufSize, name, &st, bufSize + 1);
        if (entrySize == 0 || bufSize + entrySize > size)
        {
            Logger::Log(LogLevel::WARN, "fs_readdir: Buffer full or invalid entry: " + std::string(name));
            return false;
        }

        bufSize += entrySize;
        return true;
    };

    addDirEntry(".", ino, S_IFDIR);
    addDirEntry("..", FUSE_ROOT_ID, S_IFDIR);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        for (const auto &kv : g_state->files)
        {
            if (kv.first.find(parentPath) == 0 && kv.first != parentPath)
            {
                std::string relativePath = kv.first.substr(parentPath.size());
                if (!relativePath.empty() && relativePath[0] == '/')
                {
                    relativePath = relativePath.substr(1);
                }

                if (relativePath.find('/') == std::string::npos)
                {
                    if (!addDirEntry(relativePath.c_str(), getInode(kv.first), kv.second ? S_IFREG : S_IFDIR))
                    {
                        Logger::Log(LogLevel::WARN, "fs_readdir: Failed to add entry: " + relativePath);
                        break;
                    }
                }
            }
        }
    }

    Logger::Log(LogLevel::TRACE, "fs_readdir: Returning buffer of size: " + std::to_string(bufSize));
    fuse_reply_buf(req, buf, bufSize);
    free(buf);
}

// Open callback
void fs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_open: Inode: " + std::to_string(ino) + ", Path: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);

        if (it != g_state->files.end())
        {
            // Only create StreamManager for .ts files if not already created
            if (path.ends_with(".ts") && !it->second->streamContext)
            {
                Logger::Log(LogLevel::DEBUG, "fs_open: Creating StreamManager for .ts file: " + path);
                try
                {
                    it->second->streamContext = std::make_unique<StreamManager>(it->second->url, 4 * 1024 * 1024, g_state->isShuttingDown);
                    it->second->streamContext->startStreaming();
                    Logger::Log(LogLevel::DEBUG, "fs_open: StreamManager successfully created for: " + path);
                }
                catch (const std::exception &e)
                {
                    Logger::Log(LogLevel::ERROR, "fs_open: Failed to create StreamManager for path: " + path + ". Error: " + e.what());
                    it->second->streamContext.reset(); // Ensure no dangling pointer
                    fuse_reply_err(req, ENOMEM);
                    return;
                }
            }

            // Pass the VirtualFile pointer to file handle
            fi->fh = reinterpret_cast<uint64_t>(it->second.get());
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
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_write: Writing " + std::to_string(size) + " bytes to " + path);

    std::string fullPath = g_state->storageDir + path;
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
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_release: Inode: " + std::to_string(ino) + ", Path: " + path);
    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second != nullptr && it->second->streamContext != nullptr)
    {
        if (it->second->streamContext && !it->second->streamContext->isStopped())
        {
            Logger::Log(LogLevel::DEBUG, "fs_release: Decrementing reader count for path: " + std::string(path));
            it->second->streamContext->decrementReaderCount();
        }
        Logger::Log(LogLevel::DEBUG, "fs_release: Decrementing reader count for path: " + std::string(path));
    }

    Logger::Log(LogLevel::DEBUG, "fs_release: Inode: " + std::to_string(ino));
    fuse_reply_err(req, 0);
}

// Opendir callback
void fs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_opendir: Inode: " + std::to_string(ino));
    fuse_reply_open(req, fi);
}

// Releasedir callback
void fs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_releasedir: Inode: " + std::to_string(ino));
    fuse_reply_err(req, 0);
}

void fs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_read: Inode: " + std::to_string(ino) + ", Path: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);
        if (it != g_state->files.end() && it->second)
        {
            auto vf = it->second.get();
            StreamManager *streamManager = vf->streamContext.get();

            // Handle .strm files
            if (path.ends_with(".strm"))
            {
                const auto &url = vf->url;
                if (static_cast<size_t>(off) >= url.size())
                {
                    fuse_reply_buf(req, nullptr, 0); // EOF
                    return;
                }
                size_t toRead = std::min(size, url.size() - static_cast<size_t>(off));
                fuse_reply_buf(req, url.data() + off, toRead);
                return;
            }
            // Handle .xml files
            else if (path.ends_with(".xml"))
            {
                std::string contentUrl = vf->url + ".xml";
                Logger::Log(LogLevel::DEBUG, "fs_read: Fetching XML content from: " + contentUrl);
                char *buf = new char[size];
                size_t bytesRead = streamManager->readContent(contentUrl, buf, size, off);
                fuse_reply_buf(req, buf, bytesRead);
                delete[] buf;
                return;
            }
            // Handle .m3u files
            else if (path.ends_with(".m3u"))
            {
                std::string contentUrl = vf->url + ".m3u";
                Logger::Log(LogLevel::DEBUG, "fs_read: Fetching M3U content from: " + contentUrl);
                char *buf = new char[size];
                size_t bytesRead = streamManager->readContent(contentUrl, buf, size, off);
                fuse_reply_buf(req, buf, bytesRead);
                delete[] buf;
                return;
            }
            // Handle .ts files
            else if (path.ends_with(".ts"))
            {
                if (!streamManager)
                {
                    Logger::Log(LogLevel::ERROR, "fs_read: StreamManager not found for path: " + path);
                    fuse_reply_err(req, ENOENT);
                    return;
                }

                char *buf = new char[size];
                size_t bytesRead = streamManager->getPipe().read(buf, size, g_state->isShuttingDown);

                Logger::Log(LogLevel::TRACE, "fs_read: Pipe::read returned " + std::to_string(bytesRead) + " bytes for path: " + path);

                fuse_reply_buf(req, buf, bytesRead);
                delete[] buf;
                return;
            }

            Logger::Log(LogLevel::ERROR, "fs_read: Unsupported file type for path: " + path);
            fuse_reply_err(req, EINVAL); // Unsupported file type
            return;
        }
    }

    Logger::Log(LogLevel::ERROR, "fs_read: File not found for path: " + path);
    fuse_reply_err(req, ENOENT);
}
