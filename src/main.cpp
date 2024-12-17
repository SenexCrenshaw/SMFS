#include "smfs_state.hpp"
#include "websocket_client.hpp"
#include "fuse_operations.hpp"
#include "logger.hpp"

SMFS *g_state = nullptr;

int main(int argc, char *argv[])
{
    if (argc < 5)
    {
        std::cerr << "Usage: " << argv[0] << " --host <host> --port <port> --apikey <apikey> --mount <mount_point>\n";
        return 1;
    }

    std::string host, port, apiKey, mountPoint;
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
    }

    Logger::InitLogFile("/var/log/smfs/smfs.log");
    g_state = new SMFS(host, port, apiKey);
    g_state->apiClient.fetchFileList();

    WebSocketClient wsClient(host, port, apiKey);
    wsClient.Start();

    struct fuse_operations fs_ops = {};
    fs_ops.getattr = fs_getattr;
    fs_ops.readdir = fs_readdir;

    char *fuseArgs[] = {argv[0], const_cast<char *>(mountPoint.c_str()), nullptr};
    return fuse_main(2, fuseArgs, &fs_ops, nullptr);
}
