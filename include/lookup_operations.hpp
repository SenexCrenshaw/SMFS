// File: lookup_operations.hpp
#pragma once
#define FUSE_USE_VERSION 35
#include <fuse3/fuse_lowlevel.h>
#include <string>

void fs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name);
void fs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);