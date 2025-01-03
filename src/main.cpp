// File: main.cpp
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
#include <fuse_operations.hpp>
#include <algorithm>
#include <memory>

// Global pointers
std::unique_ptr<SMFS> g_state;
std::atomic<bool> exitRequested{false};
std::unique_ptr<FuseManager> fuseManager;

// Signal handler to gracefully exit
void handleSignal(int signal)
{
    if (signal == SIGINT)
    {
        exitRequested = true; // Set exit flag
    }
}

void parseEnableFlag(const std::string &arg, std::set<std::string> &enabledFileTypes)
{
    if (arg.find("--enable-") == 0)
    {
        size_t equalPos = arg.find('=');
        if (equalPos != std::string::npos)
        {
            std::string fileType = arg.substr(9, equalPos - 9); // Extract file type
            std::string value = arg.substr(equalPos + 1);       // Extract value

            if (value == "true")
            {
                enabledFileTypes.insert(fileType);
            }
            else if (value == "false")
            {
                enabledFileTypes.erase(fileType);
            }
            else
            {
                std::cerr << "Invalid value for " << arg << ". Use true or false." << std::endl;
            }
        }
        else
        {
            std::cerr << "Invalid argument format: " << arg << ". Expected --enable-<type>=<value>" << std::endl;
        }
    }
}

void stopAllStreams()
{
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    for (auto &file : g_state->files)
    {
        if (file.second && file.second->streamContext)
        {
            Logger::Log(LogLevel::INFO, "Stopping stream for path: " + file.first);
            file.second->streamContext->stopStreaming();
            file.second->streamContext.reset();
        }
    }
}

int main(int argc, char *argv[])
{
    // Register signal handler
    std::signal(SIGINT, handleSignal);

    // Initialize application parameters
    LogLevel logLevel = LogLevel::INFO; // Default log level
    bool debugMode = false;
    std::string host = "localhost";
    std::string port = "7095";
    std::string apiKey = "APIKEY from settings.json";
    std::string mountPoint = "/mnt/smfs";
    std::string cacheDir = "/tmp/smfs_storage";
    std::string streamGroupProfileIds = "5";
    bool isShort = true;
    std::set<std::string> enabledFileTypes{"xml", "m3u", "ts"};

    // Parse arguments
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--help")
        {
            std::cout << "Usage: ./smfs [options]\n"
                      << "--debug                         Enable debug mode (equivalent to --log-level DEBUG)\n"
                      << "--log-level <level>             Set log level (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)\n"
                      << "--enable-<filetype>=true/false  Enable or disable specific file types (e.g., ts, strm, m3u, xml)\n"
                      << "--mount <mountpoint>            Set the FUSE mount point\n"
                      << "--isShort=true/false            Set the short URL\n"
                      << "--cacheDir <path>               Specify the cache directory\n";
            exit(0);
        }
        else if (arg == "--debug")
        {
            debugMode = true;
        }
        else if (arg == "--log-level" && i + 1 < argc)
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
        else if (arg == "--host")
            host = argv[++i];
        else if (arg == "--port")
            port = argv[++i];
        else if (arg == "--apikey")
            apiKey = argv[++i];
        else if (arg == "--mount")
            mountPoint = argv[++i];
        else if (arg == "--streamGroupProfileIds")
            streamGroupProfileIds = argv[++i];
        else if (arg == "--isShort")
            isShort = (std::string(argv[++i]) == "true");
        else if (arg == "--cacheDir")
            cacheDir = argv[++i];
        else
        {
            parseEnableFlag(arg, enabledFileTypes);
        }
    }

    if (debugMode)
    {
        logLevel = LogLevel::DEBUG;
    }
    // Initialize Logger
    Logger::InitLogFile("/var/log/smfs/smfs.log");
    Logger::SetLogLevel(logLevel);
    Logger::SetDebug(debugMode);
    Logger::Log(LogLevel::INFO, "SMFS starting...");

    // Initialize FuseManager
    fuseManager = std::make_unique<FuseManager>(mountPoint);
    inodeToPath[FUSE_ROOT_ID] = "/";
    pathToInode["/"] = FUSE_ROOT_ID;
    if (!fuseManager->Initialize(debugMode))
    {
        Logger::Log(LogLevel::ERROR, "Failed to initialize FUSE.");
        return 1;
    }

    // Create global SMFS state
    g_state = std::make_unique<SMFS>(host, port, apiKey, streamGroupProfileIds, isShort);

    g_state->cacheDir = cacheDir;
    Logger::Log(LogLevel::INFO, "Cache directory set to: " + cacheDir);
    g_state->enabledFileTypes = std::move(enabledFileTypes);

    for (const auto &fileType : g_state->enabledFileTypes)
    {
        Logger::Log(LogLevel::INFO, "Enabled file type: " + fileType);
    }

    // Start WebSocket client
    WebSocketClient wsClient(host, port);
    std::thread wsThread([&wsClient]()
                         {
                             Logger::Log(LogLevel::INFO, "Starting WebSocket client thread...");
                             wsClient.Start(); });

    // Run the FUSE session
    fuseManager->Run();

    // Wait for shutdown
    while (!exitRequested)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    stopAllStreams();

    wsClient.Stop();
    if (wsThread.joinable())
    {
        Logger::Log(LogLevel::INFO, "Waiting for WebSocket client thread to finish...");
        wsThread.join();
        Logger::Log(LogLevel::INFO, "WebSocket client thread joined.");
    }

    // Stop FUSE
    fuseManager->Stop();

    Logger::Log(LogLevel::INFO, "SMFS exited cleanly.");
    return 0;
}
