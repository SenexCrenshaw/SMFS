// File: file_operations.hpp

#pragma once
#define FUSE_USE_VERSION 35
#include <fuse3/fuse_lowlevel.h>

void fs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);
void fs_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi);
void fs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);