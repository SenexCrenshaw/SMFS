// File: util_operations.hpp

#pragma once
#define FUSE_USE_VERSION 35
#include <fuse3/fuse_lowlevel.h>

void fs_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi);
void fs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size);
void fs_mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev);