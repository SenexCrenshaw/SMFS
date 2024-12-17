#include "fuse_operations.hpp"
#include "smfs_state.hpp"
#include <cstring>

int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi)
{
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    if (g_state->files.find(path) != g_state->files.end())
    {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 0;
        return 0;
    }

    return -ENOENT;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi, fuse_readdir_flags flags)
{
    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    for (const auto &entry : g_state->files)
    {
        filler(buf, entry.first.c_str() + 1, NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    }
    return 0;
}

// Implement `fs_open`, `fs_read`, and `fs_release` similarly...
