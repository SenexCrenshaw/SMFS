#include "fuse_manager.hpp"
#include "fuse_operations.hpp"
#include "logger.hpp"

FuseManager::FuseManager(const std::string &mountPoint)
    : mountPoint_(mountPoint), session_(nullptr), exitRequested_(false)
{
}

FuseManager::~FuseManager()
{
    Stop();
}

bool FuseManager::Initialize(bool debugMode)
{
    std::vector<std::string> argsList = {"fuse_app", "-o", "allow_other"};
    if (debugMode)
    {
        argsList.push_back("-d");
    }
    std::vector<char *> fuseArgs;
    for (auto &arg : argsList)
    {
        fuseArgs.push_back(const_cast<char *>(arg.c_str()));
    }
    fuseArgs.push_back(nullptr);

    struct fuse_args args = FUSE_ARGS_INIT(static_cast<int>(fuseArgs.size()) - 1, fuseArgs.data());

    // Initialize FUSE operations
    struct fuse_lowlevel_ops ll_ops = {};
    ll_ops.lookup = fs_lookup;
    ll_ops.getattr = fs_getattr;
    ll_ops.readdir = fs_readdir;
    ll_ops.open = fs_open;
    ll_ops.read = fs_read;
    ll_ops.write = fs_write;
    ll_ops.setattr = fs_setattr;
    ll_ops.release = fs_release;
    ll_ops.opendir = fs_opendir;
    ll_ops.releasedir = fs_releasedir;
    ll_ops.mknod = fs_mknod;
    ll_ops.getxattr = fs_getxattr;

    session_ = fuse_session_new(&args, &ll_ops, sizeof(ll_ops), nullptr);
    if (!session_)
    {
        Logger::Log(LogLevel::ERROR, "Failed to initialize FUSE session.");
        return false;
    }

    if (fuse_session_mount(session_, mountPoint_.c_str()) != 0)
    {
        Logger::Log(LogLevel::ERROR, "Failed to mount FUSE filesystem at " + mountPoint_);
        fuse_session_destroy(session_);
        session_ = nullptr;
        return false;
    }

    Logger::Log(LogLevel::INFO, "FUSE session initialized and mounted at " + mountPoint_);
    return true;
}

void FuseManager::Run()
{
    fuseThread_ = std::thread(&FuseManager::FuseLoop, this);
}

void FuseManager::Stop()
{
    if (session_)
    {
        Logger::Log(LogLevel::INFO, "Stopping FUSE session...");
        fuse_session_exit(session_);

        if (fuseThread_.joinable())
        {
            Logger::Log(LogLevel::WARN, "FUSE thread did not exit, forcefully killing...");
            pthread_cancel(fuseThread_.native_handle()); // Force kill thread
            fuseThread_.join();
        }

        fuse_session_unmount(session_);
        fuse_session_destroy(session_);
        session_ = nullptr;

        Logger::Log(LogLevel::INFO, "FUSE session stopped.");
    }
}

void FuseManager::FuseLoop()
{
    struct fuse_loop_config config = {.clone_fd = 1, .max_idle_threads = 10};

    Logger::Log(LogLevel::DEBUG, "Starting FUSE session loop...");
    int result = fuse_session_loop_mt(session_, &config);
    if (result != 0)
    {
        Logger::Log(LogLevel::ERROR, "FUSE session loop exited with error: " + std::to_string(result));
    }
    else
    {
        Logger::Log(LogLevel::INFO, "FUSE session loop exited normally.");
    }

    {
        std::lock_guard<std::mutex> lock(exitMutex_);
        exitRequested_ = true;
    }
    exitCondition_.notify_all();
}
