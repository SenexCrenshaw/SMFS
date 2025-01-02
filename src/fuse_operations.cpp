// File: fuse_operations.cpp
#include "fuse_operations.hpp"
#include "async_curl_client.hpp"
#include "stream_manager.hpp"
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
#include <future>
#include <string.h>

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
    std::string path = parentPath + "/" + std::string(name);

    // Normalize path: Remove redundant slashes
    while (path.find("//") != std::string::npos)
    {
        path = path.replace(path.find("//"), 2, "/");
    }

    Logger::Log(LogLevel::DEBUG, "fs_lookup: Resolving parentPath: " + parentPath + "  path:  " + path);

    struct fuse_entry_param e = {};
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);

        // Search in in-memory files map
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

    // Check cacheDir for the file
    std::string cachePath = g_state->cacheDir + path;
    struct stat st;
    if (lstat(cachePath.c_str(), &st) == 0)
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);

        // Add to in-memory file map if not already present
        if (g_state->files.find(path) == g_state->files.end())
        {
            g_state->files[path] = std::make_shared<VirtualFile>(cachePath, st.st_size);
        }

        e.ino = getInode(path);
        e.attr.st_ino = e.ino;
        e.attr.st_mode = st.st_mode;
        e.attr.st_nlink = st.st_nlink;
        e.attr.st_size = st.st_size;
        e.attr.st_uid = st.st_uid;
        e.attr.st_gid = st.st_gid;
        e.attr.st_atime = st.st_atime;
        e.attr.st_mtime = st.st_mtime;
        e.attr.st_ctime = st.st_ctime;

        Logger::Log(LogLevel::DEBUG, "fs_lookup: Found file in cacheDir: " + cachePath);
        fuse_reply_entry(req, &e);
        return;
    }

    Logger::Log(LogLevel::ERROR, "fs_lookup: Path not found: " + path);
    fuse_reply_err(req, ENOENT);
}

// Getattr callback
void fs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
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

    auto it = inodeToPath.find(ino);
    if (it == inodeToPath.end())
    {
        Logger::Log(LogLevel::ERROR, "fs_getattr: Inode not found: " + std::to_string(ino));
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string path = it->second;
    Logger::Log(LogLevel::DEBUG, "fs_getattr: Path resolved for inode: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto fileIt = g_state->files.find(path);
        if (fileIt != g_state->files.end())
        {
            st.st_ino = ino;
            st.st_mode = fileIt->second ? S_IFREG | 0444 : S_IFDIR | 0755;
            st.st_nlink = fileIt->second ? 1 : 2;
            st.st_size = fileIt->second ? INT64_MAX : 0;
            Logger::Log(LogLevel::DEBUG, "fs_getattr: Returning attributes for path: " + path);
            fuse_reply_attr(req, &st, 1.0);
            return;
        }
    }

    // Check cacheDir for the file
    std::string cachePath = g_state->cacheDir + path;
    if (lstat(cachePath.c_str(), &st) == 0)
    {
        st.st_ino = ino; // Assign the correct inode
        Logger::Log(LogLevel::DEBUG, "fs_getattr: Returning attributes from cacheDir for path: " + cachePath);
        fuse_reply_attr(req, &st, 1.0);
        return;
    }

    Logger::Log(LogLevel::ERROR, "fs_getattr: Path not found: " + path);
    fuse_reply_err(req, ENOENT);
}

// Readdir callback
void fs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Inode: " + std::to_string(ino));
    Logger::Log(LogLevel::TRACE, "fs_readdir: Offset: " + std::to_string(off));

    // Signal EOF for offsets greater than 0
    if (off > 0)
    {
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Offset > 0. No more entries to return.");
        fuse_reply_buf(req, nullptr, 0);
        return;
    }

    if (inodeToPath.find(ino) == inodeToPath.end())
    {
        Logger::Log(LogLevel::ERROR, "fs_readdir: Inode not found: " + std::to_string(ino));
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string parentPath = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Parent path resolved: " + parentPath);

    if (parentPath.empty())
    {
        parentPath = "/";
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Parent Path was empty. Assuming root: /");
    }

    char *buf = (char *)calloc(1, size);
    size_t bufSize = 0;

    auto addDirEntry = [&](const std::string &name, fuse_ino_t inode, mode_t mode)
    {
        struct stat st = {};
        st.st_ino = inode;
        st.st_mode = mode;

        size_t entrySize = fuse_add_direntry(req, buf + bufSize, size - bufSize, name.c_str(), &st, bufSize + 1);
        if (entrySize == 0 || bufSize + entrySize > size)
        {
            Logger::Log(LogLevel::WARN, "fs_readdir: Buffer full or invalid entry: " + name);
            return false;
        }

        bufSize += entrySize;
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Added entry: " + name + ", inode: " + std::to_string(inode) +
                                         ", mode: " + std::to_string(mode) + ", buffer size: " + std::to_string(bufSize));
        return true;
    };

    addDirEntry(".", ino, S_IFDIR);
    addDirEntry("..", FUSE_ROOT_ID, S_IFDIR);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Processing entries for parent path: " + parentPath);

        for (const auto &kv : g_state->files)
        {
            if (kv.first.find(parentPath) == 0 && kv.first != parentPath)
            {
                std::string relativePath;

                // Handle special case where parentPath is "/"
                if (parentPath == "/")
                {
                    relativePath = kv.first;
                }
                else
                {
                    relativePath = kv.first.substr(parentPath.size());
                }

                // Ensure relativePath starts with a single '/'
                if (!relativePath.empty() && relativePath[0] == '/')
                {
                    relativePath = relativePath.substr(1);
                }
                else
                {
                    Logger::Log(LogLevel::TRACE, "fs_readdir: Skipping invalid relative path: " + relativePath);
                    continue;
                }

                // Ensure it's a direct child by checking for additional '/'
                if (relativePath.find('/') == std::string::npos)
                {
                    mode_t mode = kv.second ? S_IFREG : S_IFDIR;
                    Logger::Log(LogLevel::TRACE, "fs_readdir: Adding " + std::string(mode == S_IFDIR ? "directory" : "file") +
                                                     ": " + relativePath);
                    if (!addDirEntry(relativePath, getInode(kv.first), mode))
                    {
                        Logger::Log(LogLevel::WARN, "fs_readdir: Failed to add entry: " + relativePath);
                        break;
                    }
                }
                else
                {
                    Logger::Log(LogLevel::TRACE, "fs_readdir: Skipping non-direct child: " + relativePath);
                }
            }
        }
    }

    Logger::Log(LogLevel::DEBUG, "fs_readdir: Returning buffer of size: " + std::to_string(bufSize));
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

void fs_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi)
{
    (void)fi;
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_setattr: Modifying attributes for " + path);

    std::string fullPath = g_state->cacheDir + path;
    int res = 0;

    if (to_set & FUSE_SET_ATTR_MODE)
    {
        res = chmod(fullPath.c_str(), attr->st_mode);
        if (res == -1)
        {
            fuse_reply_err(req, errno);
            return;
        }
    }

    if (to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID))
    {
        res = chown(fullPath.c_str(), attr->st_uid, attr->st_gid);
        if (res == -1)
        {
            fuse_reply_err(req, errno);
            return;
        }
    }

    // Fetch updated attributes
    struct stat st;
    res = lstat(fullPath.c_str(), &st);
    if (res == -1)
    {
        fuse_reply_err(req, errno);
        return;
    }

    fuse_reply_attr(req, &st, 1.0);
}

void fs_mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev)
{
    (void)rdev;
    std::string parentPath = inodeToPath[parent];
    std::string path = parentPath + "/" + name;

    Logger::Log(LogLevel::DEBUG, "fs_mknod: Creating file " + path);

    // Redirect file creation to cacheDir
    std::string fullPath = g_state->cacheDir + path;

    // Ensure parent directories exist
    size_t pos = 0;
    while ((pos = fullPath.find('/', pos + 1)) != std::string::npos)
    {
        std::string subDir = fullPath.substr(0, pos);
        if (mkdir(subDir.c_str(), 0755) == -1 && errno != EEXIST)
        {
            Logger::Log(LogLevel::ERROR, "fs_mknod: Failed to create directory " + subDir + ": " + strerror(errno));
            fuse_reply_err(req, errno);
            return;
        }
    }

    // Create the file
    int fd = open(fullPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
    if (fd == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_mknod: Failed to create file " + fullPath + ": " + strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }
    close(fd);

    struct stat st;
    if (lstat(fullPath.c_str(), &st) == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_mknod: Failed to stat file " + fullPath + ": " + strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }

    struct fuse_entry_param e = {};
    e.ino = getInode(path);
    e.attr = st;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;

    Logger::Log(LogLevel::DEBUG, "fs_mknod: File created successfully at " + fullPath);
    fuse_reply_entry(req, &e);
}

// Opendir callback
void fs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
    Logger::Log(LogLevel::DEBUG, "fs_opendir: Inode: " + std::to_string(ino));
    fuse_reply_open(req, fi);
}

// Releasedir callback
void fs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
    Logger::Log(LogLevel::DEBUG, "fs_releasedir: Inode: " + std::to_string(ino));
    fuse_reply_err(req, 0);
}

void fs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size)
{
    (void)size;
    Logger::Log(LogLevel::DEBUG, "fs_getxattr: Inode: " + std::to_string(ino) + ", Name: " + std::string(name));

    // Extended attributes are not implemented
    fuse_reply_err(req, ENOTSUP);
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
