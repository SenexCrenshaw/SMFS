// File: util_operations.cpp
#include "util_operations.hpp"
#include <logger.hpp>
#include <unordered_map>
#include <atomic>
#include <smfs_state.hpp>
#include <unistd.h>
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

void fs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size)
{
    (void)size;
    Logger::Log(LogLevel::DEBUG, "fs_getxattr: Inode: " + std::to_string(ino) + ", Name: " + std::string(name));

    // Extended attributes are not implemented
    fuse_reply_err(req, ENOTSUP);
}