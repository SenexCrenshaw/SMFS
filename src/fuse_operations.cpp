#include "fuse_operations.hpp"

int fs_getattr(const char *path, struct stat *stbuf, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_getattr called for path: " + std::string(path));
    memset(stbuf, 0, sizeof(struct stat));

    // Root directory
    if (strcmp(path, "/") == 0)
    {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        Logger::Log(LogLevel::DEBUG, "fs_getattr: Root directory accessed.");
        return 0;
    }

    // Check if the file exists in g_state
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    if (g_state->files.find(path) != g_state->files.end())
    {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = 1024; // Dummy file size
        Logger::Log(LogLevel::DEBUG, "fs_getattr: File found -> " + std::string(path));
        return 0;
    }

    Logger::Log(LogLevel::WARN, "fs_getattr: File not found -> " + std::string(path));
    return -ENOENT; // File not found
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, fuse_file_info *fi, fuse_readdir_flags flags)
{
    Logger::Log(LogLevel::DEBUG, "fs_readdir called for path: " + std::string(path));

    // Fill the root directory with "." and ".."
    filler(buf, ".", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    filler(buf, "..", NULL, 0, static_cast<fuse_fill_dir_flags>(0));
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Added '.' and '..'");

    // Add files from g_state->files
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    for (const auto &entry : g_state->files)
    {
        filler(buf, entry.first.c_str() + 1, NULL, 0, static_cast<fuse_fill_dir_flags>(0));
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Added file -> " + entry.first);
    }

    return 0;
}

int fs_open(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_open called for path: " + std::string(path));

    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    if (g_state->files.find(path) != g_state->files.end())
    {
        Logger::Log(LogLevel::DEBUG, "fs_open: File opened -> " + std::string(path));
        return 0; // File exists, allow open
    }

    Logger::Log(LogLevel::WARN, "fs_open: File not found -> " + std::string(path));
    return -ENOENT; // File not found
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_read called for path: " + std::string(path) +
                                     ", size: " + std::to_string(size) + ", offset: " + std::to_string(offset));

    static const char *dummyContent = "Hello, this is a test file content!\n";
    size_t len = strlen(dummyContent);

    if (offset >= len)
    {
        Logger::Log(LogLevel::DEBUG, "fs_read: EOF reached for path: " + std::string(path));
        return 0; // EOF
    }

    size_t to_read = std::min(size, len - offset);
    memcpy(buf, dummyContent + offset, to_read);

    Logger::Log(LogLevel::DEBUG, "fs_read: Read " + std::to_string(to_read) + " bytes from file -> " + std::string(path));
    return to_read;
}

int fs_release(const char *path, fuse_file_info *fi)
{
    Logger::Log(LogLevel::DEBUG, "fs_release called for path: " + std::string(path));
    return 0; // Nothing to clean up
}
