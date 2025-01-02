#pragma once
#define FUSE_USE_VERSION 35
#include <fuse3/fuse_lowlevel.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

class FuseManager
{
public:
    FuseManager(const std::string &mountPoint);
    ~FuseManager();

    bool Initialize(bool debugMode);
    void Run();
    void Stop();

private:
    std::string mountPoint_;

    struct fuse_session *session_;
    std::thread fuseThread_;
    std::mutex exitMutex_;
    std::condition_variable exitCondition_;
    bool exitRequested_;

    void FuseLoop();
};
