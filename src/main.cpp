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

// Global pointer
SMFS *g_state = nullptr;

int main(int argc, char *argv[])
{
    // if (argc < 5)
    // {
    //     std::cerr << "Usage: " << argv[0]
    //               << " --host <host> --port <port> --apikey <apikey> --mount <mount_point>"
    //               << " [--streamGroupProfileIds <ids>] [--isShort <true|false>] [--debug]"
    //               << std::endl;
    //     return 1;
    // }

    bool debugMode = false;
    std::string host = "10.3.10.50";
    std::string port = "7095";
    std::string apiKey = "f4bed758a1aa45a38c801ed6893d70fb";
    std::string mountPoint = "/mnt/fuse";
    std::string storageDir = "/tmp/smfs_storage"; // default
    std::string streamGroupProfileIds = "3";      // Default value
    bool isShort = true;                          // Default value
    std::set<std::string> enabledFileTypes{"xml", "m3u", "strm"};

    // Helper lambda to process `--enable-<filetype>=true/false` or `--enable-<filetype>`
    auto parseEnableFlag = [&](const std::string &arg, const std::string &fileType)
    {
        std::string prefix = "--enable-" + fileType;
        if (arg.find(prefix) == 0)
        {
            if (arg == prefix) // e.g., --enable-m3u (defaults to true)
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

    // Parse arguments
    for (int i = 1; i < argc; i++)
    {
        if (std::string(argv[i]) == "--host")
            host = argv[++i];
        else if (std::string(argv[i]) == "--port")
            port = argv[++i];
        else if (std::string(argv[i]) == "--apikey")
            apiKey = argv[++i];
        else if (std::string(argv[i]) == "--mount")
            mountPoint = argv[++i];
        else if (std::string(argv[i]) == "--debug")
            debugMode = true;
        else if (std::string(argv[i]) == "--streamGroupProfileIds")
            streamGroupProfileIds = argv[++i];
        else if (std::string(argv[i]) == "--isShort")
            isShort = (std::string(argv[++i]) == "true");
        else if (std::string(argv[i]) == "--storageDir") // NEW
            storageDir = argv[++i];
        // else if (std::string(argv[i]) == "--enable-m3u")
        //     enabledFileTypes.insert("m3u");
        // else if (std::string(argv[i]) == "--enable-xml")
        //     enabledFileTypes.insert("xml");
        // else if (std::string(argv[i]) == "--enable-ts")
        //     enabledFileTypes.insert("ts");
        // else if (std::string(argv[i]) == "--enable-strm")
        //     enabledFileTypes.insert("strm");
        else
        {
            // Handle --enable-<filetype>=true/false
            parseEnableFlag(argv[i], "m3u");
            parseEnableFlag(argv[i], "xml");
            parseEnableFlag(argv[i], "strm");
            parseEnableFlag(argv[i], "ts");
        }
    }

    // Initialize Logger
    Logger::InitLogFile("/var/log/smfs/smfs.log");
    // ^ Ensure this path exists or adjust to your system
    std::cout << "[INFO] SMFS starting..." << std::endl;

    // Create global SMFS state
    g_state = new SMFS(host, port, apiKey, streamGroupProfileIds, isShort);
    g_state->storageDir = storageDir;

    // Pass enabled file types to the global state
    g_state->enabledFileTypes = std::move(enabledFileTypes);

    // Fetch initial file list from your REST API
    g_state->apiClient.fetchFileList();

    // Start WebSocket client in another thread
    WebSocketClient wsClient(host, port, apiKey);
    std::thread wsThread([&wsClient]()
                         {
        std::cout << "[INFO] Starting WebSocket client thread..." << std::endl;
        wsClient.Start(); });

    // Delay to ensure WebSocket starts before fuse_main
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Setup FUSE operations
    struct fuse_operations fs_ops = {};
    fs_ops.getattr = fs_getattr;
    fs_ops.readdir = fs_readdir;
    fs_ops.open = fs_open;
    fs_ops.read = fs_read;
    fs_ops.release = fs_release;
    fs_ops.opendir = fs_opendir;
    fs_ops.releasedir = fs_releasedir;
    fs_ops.create = fs_create;
    fs_ops.write = fs_write;
    fs_ops.chmod = fs_chmod;

    std::cout << "[INFO] Starting FUSE filesystem on mount point: " << mountPoint << std::endl;

    // Build fuse arguments
    std::vector<char *> fuseArgs;
    fuseArgs.push_back(argv[0]);
    if (debugMode)
    {
        fuseArgs.push_back(const_cast<char *>("-d")); // debug mode
    }
    fuseArgs.push_back(const_cast<char *>("-f")); // foreground mode

    fuseArgs.push_back(const_cast<char *>("-o"));
    fuseArgs.push_back(const_cast<char *>("allow_other"));

    fuseArgs.push_back(const_cast<char *>(mountPoint.c_str()));
    fuseArgs.push_back(nullptr);

    int fuseStatus = fuse_main(
        static_cast<int>(fuseArgs.size()) - 1,
        fuseArgs.data(),
        &fs_ops,
        nullptr);

    std::cout << "[DEBUG] fuse_main exited with status: " << fuseStatus << std::endl;

    // Stop WebSocket client gracefully
    wsClient.Stop();
    if (wsThread.joinable())
    {
        wsThread.join();
    }

    std::cout << "[INFO] SMFS exiting." << std::endl;
    return fuseStatus;
}
