#pragma once
#define FUSE_USE_VERSION 35

#include <fuse3/fuse.h> // FUSE3 header

#include <sys/stat.h> // For S_IFDIR, S_IFREG, and struct stat
#include <cstring>    // For memset
#include <string>
#include <mutex>
#include "../src/smfs_state.hpp"
#include "logger.hpp"

// Declare FUSE operation functions
int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi);
int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi, fuse_readdir_flags flags);
int fs_open(const char *path, fuse_file_info *fi);
int fs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi);
int fs_release(const char *path, fuse_file_info *fi);
