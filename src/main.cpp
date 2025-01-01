#include "smfs_state.hpp"
#include "websocket_client.hpp"
#include "fuse_operations.hpp"
#include "logger.hpp"

#include <thread>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>
#include <cstring>
#include <set>
#include <csignal>
#include <fuse3/fuse_lowlevel.h>

// Global pointers
SMFS *g_state = nullptr;
std::atomic<bool> exitRequested{false};
struct fuse_session *g_fuseSession = nullptr;

// Signal handler to gracefully exit
void handleSignal(int signal)
{
    if (signal == SIGINT)
    {
        Logger::Log(LogLevel::INFO, "SIGINT received. Exiting...");
        if (g_fuseSession != nullptr)
        {
            Logger::Log(LogLevel::DEBUG, "Calling fuse_session_exit...");
            fuse_session_exit(g_fuseSession);
        }
        exitRequested = true;
    }
}

// Main function
int main(int argc, char *argv[])
{
    // Register signal handler
    std::signal(SIGINT, handleSignal);

    // Initialize application parameters
    LogLevel logLevel = LogLevel::INFO; // Default log level
    std::string host = "10.3.10.50";
    std::string port = "7095";
    std::string apiKey = "f4bed758a1aa45a38c801ed6893d70fb";
    std::string mountPoint = "/mnt/fuse";
    std::string storageDir = "/tmp/smfs_storage";
    std::string streamGroupProfileIds = "3";
    bool isShort = true;
    std::set<std::string> enabledFileTypes{"xml", "m3u", "strm"};

    // Parse arguments
    auto parseEnableFlag = [&](const std::string &arg, const std::string &fileType)
    {
        std::string prefix = "--enable-" + fileType;
        if (arg.find(prefix) == 0)
        {
            if (arg == prefix)
            {
                enabledFileTypes.insert(fileType);
            }
            else if (arg.find('=') != std::string::npos)
            {
                std::string value = arg.substr(arg.find('=') + 1);
                if (value == "false")
                {
                    enabledFileTypes.erase(fileType);
                }
                else if (value == "true")
                {
                    enabledFileTypes.insert(fileType);
                }
            }
        }
    };

    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == "--help")
        {
            std::cout << "Usage: ./smfs [options]\n"
                      << "--log-level <level>             Set log level (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)\n"
                      << "--enable-<filetype>=true/false  Enable or disable specific file types (e.g., ts, strm, m3u, xml)\n"
                      << "--mount <mountpoint>            Set the FUSE mount point\n"
                      << "--storageDir <path>             Specify the storage directory\n";
            exit(0);
        }
        else if (std::string(argv[i]) == "--log-level" && i + 1 < argc)
        {
            std::string level = argv[++i];

            // Convert the input level to lowercase
            std::transform(level.begin(), level.end(), level.begin(), ::tolower);

            if (level == "trace")
                logLevel = LogLevel::TRACE;
            else if (level == "debug")
                logLevel = LogLevel::DEBUG;
            else if (level == "info")
                logLevel = LogLevel::INFO;
            else if (level == "warn")
                logLevel = LogLevel::WARN;
            else if (level == "error")
                logLevel = LogLevel::ERROR;
            else if (level == "fatal")
                logLevel = LogLevel::FATAL;
            else
            {
                std::cerr << "Invalid log level: " << level << std::endl;
                return 1;
            }
            std::cout << "Configured log level: " << level << std::endl;
        }
        else if (std::string(argv[i]) == "--host")
            host = argv[++i];
        else if (std::string(argv[i]) == "--port")
            port = argv[++i];
        else if (std::string(argv[i]) == "--apikey")
            apiKey = argv[++i];
        else if (std::string(argv[i]) == "--mount")
            mountPoint = argv[++i];
        else if (std::string(argv[i]) == "--streamGroupProfileIds")
            streamGroupProfileIds = argv[++i];
        else if (std::string(argv[i]) == "--isShort")
            isShort = (std::string(argv[++i]) == "true");
        else if (std::string(argv[i]) == "--storageDir")
            storageDir = argv[++i];
        else
        {
            parseEnableFlag(argv[i], "m3u");
            parseEnableFlag(argv[i], "xml");
            parseEnableFlag(argv[i], "strm");
            parseEnableFlag(argv[i], "ts");
        }
    }

    // Initialize Logger
    Logger::InitLogFile("/var/log/smfs/smfs.log");
    Logger::SetLogLevel(logLevel);
    Logger::Log(LogLevel::INFO, "SMFS starting...");

    // Create global SMFS state
    g_state = new SMFS(host, port, apiKey, streamGroupProfileIds, isShort);
    // Initialize root inode mapping
    inodeToPath[FUSE_ROOT_ID] = "";
    pathToInode["/"] = FUSE_ROOT_ID;
    Logger::Log(LogLevel::DEBUG, "Initialized root inode mapping.");

    g_state->storageDir = storageDir;
    g_state->enabledFileTypes = std::move(enabledFileTypes);

    for (const auto &fileType : enabledFileTypes)
    {
        Logger::Log(LogLevel::INFO, "Enabled file type: " + fileType);
    }

    // Fetch initial file list
    g_state->apiClient.fetchFileList();

    // Start WebSocket client
    WebSocketClient wsClient(host, port, apiKey);
    std::thread wsThread([&wsClient]()
                         {
        Logger::Log(LogLevel::INFO, "Starting WebSocket client thread...");
        wsClient.Start(); });

    // FUSE low-level operations
    struct fuse_lowlevel_ops ll_ops = {};
    ll_ops.lookup = fs_lookup;
    ll_ops.getattr = fs_getattr;
    ll_ops.readdir = fs_readdir;
    ll_ops.open = fs_open;
    ll_ops.read = fs_read;
    ll_ops.write = fs_write;
    ll_ops.release = fs_release;
    ll_ops.opendir = fs_opendir;
    ll_ops.releasedir = fs_releasedir;

    // Setup FUSE arguments
    std::vector<std::string> argsList;
    argsList.push_back(argv[0]);
    argsList.push_back("-o");
    argsList.push_back("allow_other");
    std::vector<char *> fuseArgs;
    for (auto &arg : argsList)
    {
        fuseArgs.push_back(const_cast<char *>(arg.c_str()));
    }
    fuseArgs.push_back(nullptr);
    struct fuse_args args = FUSE_ARGS_INIT(static_cast<int>(fuseArgs.size()) - 1, fuseArgs.data());
    for (const auto &arg : argsList)
    {
        Logger::Log(LogLevel::DEBUG, "FUSE arg: " + arg);
    }

    // Initialize FUSE session
    g_fuseSession = fuse_session_new(&args, &ll_ops, sizeof(ll_ops), nullptr);
    if (!g_fuseSession)
    {
        Logger::Log(LogLevel::ERROR, "Failed to initialize FUSE session.");
        return 1;
    }

    if (fuse_session_mount(g_fuseSession, mountPoint.c_str()) != 0)
    {
        Logger::Log(LogLevel::ERROR, "Failed to mount FUSE filesystem at " + mountPoint);
        fuse_session_destroy(g_fuseSession);
        return 1;
    }

    Logger::Log(LogLevel::INFO, "Starting FUSE multithreaded event loop.");
    struct fuse_loop_config config = {};
    config.clone_fd = 1;
    config.max_idle_threads = 10;
    int result = fuse_session_loop_mt(g_fuseSession, &config);
    if (result != 0)
    {
        Logger::Log(LogLevel::ERROR, "FUSE multithreaded loop exited with error: " + std::to_string(result));
    }

    Logger::Log(LogLevel::INFO, "Exiting FUSE session.");
    fuse_session_unmount(g_fuseSession);
    fuse_session_destroy(g_fuseSession);

    try
    {
        Logger::Log(LogLevel::INFO, "Stopping WebSocket client...");
        wsClient.Stop();
        if (wsThread.joinable())
        {
            Logger::Log(LogLevel::DEBUG, "Joining WebSocket client thread...");
            wsThread.join();
        }
    }
    catch (const std::exception &ex)
    {
        Logger::Log(LogLevel::ERROR, "Exception during WebSocket thread shutdown: " + std::string(ex.what()));
    }

    delete g_state;
    Logger::Log(LogLevel::INFO, "SMFS exited cleanly.");
    return 0;
}
