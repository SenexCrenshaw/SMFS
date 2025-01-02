#pragma once
#define FUSE_USE_VERSION 35

#include <fuse3/fuse_lowlevel.h> // For low-level FUSE operations
#include <unordered_map>
#include <string>
#include <atomic>
#include <memory>
#include <mutex>
#include "stream_manager.hpp"
#include <smfs_state.hpp>

// Global state
extern std::unordered_map<std::string, fuse_ino_t> pathToInode;
extern std::unordered_map<fuse_ino_t, std::string> inodeToPath;
extern std::atomic<fuse_ino_t> nextInode;

// FUSE operation callbacks
void fs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name);
void fs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);
void fs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);

// Optional FUSE callbacks
void fs_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi);
void fs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi);
void fs_mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev);
void fs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size);
// Helper function
fuse_ino_t getInode(const std::string &path);
