// File: directory_operations.hpp
#pragma once
#define FUSE_USE_VERSION 35
#include <fuse3/fuse_lowlevel.h>
#include <string>

void fs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);