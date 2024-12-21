#pragma once
#define FUSE_USE_VERSION 35

#include <fuse3/fuse.h>
#include <string>
#include <sys/stat.h>
#include <cstring>
#include "../src/smfs_state.hpp"

#define BUFFER_CAPACITY (1024 * 1024)        // 1 MB buffer
#define MAX_READ_SIZE 4096                   // Typical read size
#define SOME_THRESHOLD (BUFFER_CAPACITY / 4) // Set threshold to 256 KB

// FUSE callbacks
int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi);
int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
               off_t offset, fuse_file_info *fi, fuse_readdir_flags flags);
int fs_open(const char *path, fuse_file_info *fi);
int fs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi);
int fs_release(const char *path, fuse_file_info *fi);
int fs_opendir(const char *path, fuse_file_info *fi);
int fs_releasedir(const char *path, fuse_file_info *fi);
int fs_create(const char *path, mode_t mode, fuse_file_info *fi);
int fs_write(const char *path, const char *buf, size_t size, off_t offset, fuse_file_info *fi);
int fs_chmod(const char *path, mode_t mode, fuse_file_info *fi);
