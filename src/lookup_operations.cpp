// File: lookup_operations.cpp
#include "lookup_operations.hpp"
#include <logger.hpp>
#include <smfs_state.hpp>
#include <fuse_operations.hpp>
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
