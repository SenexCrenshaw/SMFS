#include "fuse_manager.hpp"
#include "smfs_state.hpp"
#include "websocket_client.hpp"
#include "logger.hpp"

#include <thread>
#include <atomic>
#include <csignal>
#include <iostream>
#include <vector>
#include <set>
#include <cstring>
#include <chrono>

// Global pointers
SMFS *g_state = nullptr;
std::atomic<bool> exitRequested{false};
FuseManager *fuseManager = nullptr;

// Signal handler to gracefully exit
void handleSignal(int signal)
{
    if (signal == SIGINT)
    {
        Logger::Log(LogLevel::INFO, "SIGINT received. Exiting...");
        if (fuseManager)
        {
            fuseManager->Stop();
        }
        exitRequested = true;
        Logger::Log(LogLevel::INFO, "Signal handler completed. Waiting for shutdown...");
    }
}

int main(int argc, char *argv[])
{
    // Register signal handler
    std::signal(SIGINT, handleSignal);

    // Initialize application parameters
    LogLevel logLevel = LogLevel::INFO; // Default log level
    bool debugMode = false;
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
                      << "--debug                         Enable debug mode (equivalent to --log-level DEBUG)\n"
                      << "--log-level <level>             Set log level (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)\n"
                      << "--enable-<filetype>=true/false  Enable or disable specific file types (e.g., ts, strm, m3u, xml)\n"
                      << "--mount <mountpoint>            Set the FUSE mount point\n"
                      << "--storageDir <path>             Specify the storage directory\n";
            exit(0);
        }
        else if (std::string(argv[i]) == "--debug")
        {
            debugMode = true;
            logLevel = LogLevel::DEBUG;
        }
        else if (std::string(argv[i]) == "--log-level" && i + 1 < argc)
        {
            std::string level = argv[++i];
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
    Logger::SetDebug(debugMode);
    Logger::Log(LogLevel::INFO, "SMFS starting...");

    // Initialize FuseManager
    FuseManager manager(mountPoint);
    fuseManager = &manager;

    if (!manager.Initialize())
    {
        Logger::Log(LogLevel::ERROR, "Failed to initialize FUSE.");
        return 1;
    }

    // Create global SMFS state
    g_state = new SMFS(host, port, apiKey, streamGroupProfileIds, isShort);
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

    // Run the FUSE session
    manager.Run();

    // Wait for shutdown
    while (!exitRequested)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // Logger::Log(LogLevel::DEBUG, "FUSE thread state: joinable=" + std::to_string(fuseThread_.joinable()));
    // Logger::Log(LogLevel::DEBUG, "Session state: session_=" + std::string(session_ ? "valid" : "null"));

    Logger::Log(LogLevel::INFO, "Stopping WebSocket client...");
    wsClient.Stop();
    if (wsThread.joinable())
    {
        wsThread.join();
    }

    Logger::Log(LogLevel::INFO, "SMFS exited cleanly.");
    delete g_state;
    return 0;
}
