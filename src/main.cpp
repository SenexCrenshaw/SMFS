#include "smfs_state.hpp"
#include "websocket_client.hpp"
#include "fuse_operations.hpp"
#include "logger.hpp"

#include <thread>
#include <atomic>
#include <chrono>

SMFS *g_state = nullptr;

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        std::cerr << "Usage: " << argv[0] << " --host <host> --port <port> --apikey <apikey> --mount <mount_point>\n";
        return 1;
    }

    bool debugMode = false;
    std::string host, port, apiKey, mountPoint;
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
    }

    // Initialize Logger
    Logger::InitLogFile("/var/log/smfs/smfs.log");
    std::cout << "[INFO] SMFS starting..." << std::endl;

    g_state = new SMFS(host, port, apiKey);

    // Fetch initial file list
    g_state->apiClient.fetchFileList();

    // Start WebSocket client
    WebSocketClient wsClient(host, port, apiKey);
    std::thread wsThread([&wsClient]()
                         {
                             std::cout << "[INFO] Starting WebSocket client thread..." << std::endl;
                             wsClient.Start(); });

    // Delay to ensure WebSocket starts before fuse_main
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // FUSE Operations
    struct fuse_operations fs_ops = {};
    fs_ops.getattr = fs_getattr;
    fs_ops.readdir = fs_readdir;
    fs_ops.open = fs_open;
    fs_ops.read = fs_read;
    fs_ops.release = fs_release;

    std::cout << "[INFO] Starting FUSE filesystem on mount point: " << mountPoint << std::endl;

    std::vector<char *> fuseArgs = {argv[0]};
    if (debugMode)
    {
        fuseArgs.push_back(const_cast<char *>("-d"));
    }
    fuseArgs.push_back(const_cast<char *>(mountPoint.c_str()));
    fuseArgs.push_back(nullptr);

    int fuseStatus = fuse_main(fuseArgs.size() - 1, fuseArgs.data(), &fs_ops, nullptr);
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
