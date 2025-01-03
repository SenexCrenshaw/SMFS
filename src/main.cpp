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
#include <fstream>
#include <nlohmann/json.hpp>

// Global pointers
std::unique_ptr<SMFS> g_state;
std::atomic<bool> exitRequested{false};
std::unique_ptr<FuseManager> fuseManager;

void loadConfig(const std::string &configPath, std::string &host, std::string &port, std::string &apiKey,
                std::string &mountPoint, std::string &cacheDir, std::set<std::string> &enabledFileTypes,
                bool &isShort, LogLevel &logLevel)
{
    std::ifstream configFile(configPath);
    if (!configFile.is_open())
    {
        throw std::runtime_error("Failed to open config file: " + configPath);
    }

    nlohmann::json config;
    configFile >> config;

    host = config.value("host", host);
    port = config.value("port", port);
    apiKey = config.value("apiKey", apiKey);
    mountPoint = config.value("mountPoint", mountPoint);
    cacheDir = config.value("cacheDir", cacheDir);
    logLevel = config.value("logLevel", logLevel);

    if (config.contains("enabledFileTypes") && config["enabledFileTypes"].is_array())
    {
        enabledFileTypes.clear();
        for (const auto &type : config["enabledFileTypes"])
        {
            enabledFileTypes.insert(type.get<std::string>());
        }
    }

    isShort = config.value("isShort", isShort);
}

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

    // Initialize application parameters with defaults
    LogLevel logLevel = LogLevel::INFO; // Default log level
    bool debugMode = false;
    std::string host = "localhost";
    std::string port = "7095";
    std::string apiKey;
    std::string mountPoint = "/mnt/smfs";
    std::string cacheDir = "/tmp/smfs_storage";
    std::string streamGroupProfileIds;
    bool isShort = true;
    std::set<std::string> enabledFileTypes{"xml", "m3u", "ts"};

    // Check for --config option and load configuration file
    std::string configFilePath = "/etc/smfs/smconfig.json"; // Default config file path
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc)
        {
            configFilePath = argv[++i];
        }
    }

    try
    {
        loadConfig(configFilePath, host, port, apiKey, mountPoint, cacheDir, enabledFileTypes, isShort, logLevel);
        Logger::Log(LogLevel::INFO, "Configuration loaded from: " + configFilePath);
    }
    catch (const std::exception &e)
    {
        Logger::Log(LogLevel::WARN, "Failed to load config file: " + std::string(e.what()));
    }

    // Parse command line arguments to override defaults and config file
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--help")
        {
            std::cout << "Usage: ./smfs [options]\n"
                      << "--config <path>                 Path to the configuration file\n"
                      << "--debug                         Enable debug mode (equivalent to --log-level DEBUG)\n"
                      << "--log-level <level>             Set log level (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)\n"
                      << "--host <host>                   Set the API host\n"
                      << "--port <port>                   Set the API port\n"
                      << "--apikey <key>                  Set the API key\n"
                      << "--mount <mountpoint>            Set the FUSE mount point\n"
                      << "--isShort=true/false            Set the short URL\n"
                      << "--cacheDir <path>               Specify the cache directory\n"
                      << "--enable-<filetype>=true/false  Enable or disable specific file types (e.g., ts, strm, m3u, xml)\n";
            exit(0);
        }
        else if (arg == "--debug")
        {
            debugMode = true;
            logLevel = LogLevel::DEBUG;
        }
        else if (arg == "--log-level" && i + 1 < argc)
        {
            logLevel = Logger::ParseLogLevel(argv[++i]);
        }
        else if (arg == "--host" && i + 1 < argc)
        {
            host = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            port = argv[++i];
        }
        else if (arg == "--apikey" && i + 1 < argc)
        {
            apiKey = argv[++i];
        }
        else if (arg == "--mount" && i + 1 < argc)
        {
            mountPoint = argv[++i];
        }
        else if (arg == "--streamGroupProfileIds" && i + 1 < argc)
        {
            streamGroupProfileIds = argv[++i];
        }
        else if (arg == "--isShort" && i + 1 < argc)
        {
            isShort = (std::string(argv[++i]) == "true");
        }
        else if (arg == "--cacheDir" && i + 1 < argc)
        {
            cacheDir = argv[++i];
        }
        else
        {
            parseEnableFlag(arg, enabledFileTypes);
        }
    }

    // Validate required parameters
    if (host.empty() || apiKey.empty())
    {
        std::cerr << "Error: --host and --apikey must be provided either in the config file or as command line arguments." << std::endl;
        return 1;
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
