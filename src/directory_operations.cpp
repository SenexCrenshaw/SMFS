// File: directory_operations.cpp
#include "directory_operations.hpp"
#include <logger.hpp>
#include <fuse_operations.hpp>
#include <smfs_state.hpp>

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
