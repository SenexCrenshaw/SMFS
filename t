// Start of async_curl_client.cpp
#include "async_curl_client.hpp"
#include "logger.hpp"

AsyncCurlClient::AsyncCurlClient()
{
    curl_global_init(CURL_GLOBAL_ALL);
    multiHandle_ = curl_multi_init();
    workerThread_ = std::thread(&AsyncCurlClient::eventLoop, this);
}

AsyncCurlClient::~AsyncCurlClient()
{
    isRunning_ = false;
    if (workerThread_.joinable())
    {
        workerThread_.join();
    }
    curl_multi_cleanup(multiHandle_);
    curl_global_cleanup();
}

void AsyncCurlClient::fetchStreamAsync(const std::string &url, std::function<void(const std::string &data)> onDataReceived)
{
    CURL *easyHandle = curl_easy_init();
    if (!easyHandle)
    {
        throw std::runtime_error("Failed to initialize CURL easy handle");
    }

    std::string *responseData = new std::string();
    curl_easy_setopt(easyHandle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easyHandle, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(easyHandle, CURLOPT_WRITEDATA, responseData);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks_[easyHandle] = [onDataReceived, responseData]()
        {
            onDataReceived(*responseData);
            delete responseData;
        };
    }

    curl_multi_add_handle(multiHandle_, easyHandle);
}

size_t AsyncCurlClient::writeCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    auto *data = static_cast<std::string *>(userp);
    data->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

void AsyncCurlClient::eventLoop()
{
    while (isRunning_)
    {
        int runningHandles;
        curl_multi_perform(multiHandle_, &runningHandles);

        int numMessages;
        CURLMsg *msg;
        while ((msg = curl_multi_info_read(multiHandle_, &numMessages)))
        {
            if (msg->msg == CURLMSG_DONE)
            {
                CURL *easyHandle = msg->easy_handle;

                std::function<void()> callback;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = callbacks_.find(easyHandle);
                    if (it != callbacks_.end())
                    {
                        callback = std::move(it->second);
                        callbacks_.erase(it);
                    }
                }

                if (callback)
                {
                    callback();
                }

                curl_multi_remove_handle(multiHandle_, easyHandle);
                curl_easy_cleanup(easyHandle);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Polling interval
    }
}

// End of async_curl_client.cpp

// Start of api_client.cpp
// File: api_client.cpp
#include "api_client.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include "logger.hpp"
#include "smfs_state.hpp"

using json = nlohmann::json;

// Callback to write response data
static size_t write_response(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *response = reinterpret_cast<std::string *>(userdata);
    response->append(reinterpret_cast<char *>(ptr), size * nmemb);
    return size * nmemb;
}

APIClient::APIClient(const std::string &host,
                     const std::string &port,
                     const std::string &apiKey,
                     const std::string &streamGroupProfileIds,
                     bool isShort)
{
    // Example: "http://localhost:7095/api/files/getsmfs/testkey/2/true"
    baseUrl = "http://" + host + ":" + port + "/api/files/getsmfs/" + apiKey + "/" + streamGroupProfileIds + "/" + (isShort ? "true" : "false");
}

const std::map<int, SGFS> &APIClient::getGroups() const
{
    return groups;
}

void APIClient::fetchFileList()
{
    Logger::Log(LogLevel::INFO, "Fetching file list from API: " + baseUrl);
    int retries = 0;
    const int maxRetries = 5;
    int retryDelay = 1;

    while (retries < maxRetries)
    {
        try
        {
            CURL *curl = curl_easy_init();
            if (!curl)
                throw std::runtime_error("CURL initialization failed");

            std::string response;
            curl_easy_setopt(curl, CURLOPT_URL, baseUrl.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK)
                throw std::runtime_error(curl_easy_strerror(res));

            processResponse(response);
            Logger::Log(LogLevel::INFO, "File list fetched successfully.");
            return; // Exit on success
        }
        catch (const std::exception &e)
        {
            retries++;
            Logger::Log(LogLevel::WARN, "Failed to fetch file list: " + std::string(e.what()) +
                                            ". Retrying in " + std::to_string(retryDelay) + " seconds.");
            std::this_thread::sleep_for(std::chrono::seconds(retryDelay));
            retryDelay = std::min(retryDelay * 2, 32); // Exponential backoff
        }
    }

    Logger::Log(LogLevel::ERROR, "Max retries reached. Could not fetch file list.");
}

void APIClient::processResponse(const std::string &response)
{
    try
    {
        // Debug log the received JSON response
        Logger::Log(LogLevel::DEBUG, "Received JSON response: " + response);

        auto jsonResponse = json::parse(response);
        groups.clear();

        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        g_state->files.clear();

        auto isFileTypeEnabled = [](const std::string &fileName)
        {
            size_t dotPos = fileName.find_last_of('.');
            if (dotPos == std::string::npos)
                return false;                                    // No extension, not enabled
            std::string extension = fileName.substr(dotPos + 1); // Extract extension
            return g_state->enabledFileTypes.find(extension) != g_state->enabledFileTypes.end();
        };

        for (auto &entry : jsonResponse.items())
        {
            int groupId = std::stoi(entry.key());
            const auto &groupJson = entry.value();

            SGFS group;
            group.name = groupJson.value("name", "");
            group.url = groupJson.value("url", "");

            std::string groupDir = "/" + group.name;
            g_state->files[groupDir] = nullptr; // Directory
            Logger::Log(LogLevel::DEBUG, "Created group directory: " + groupDir);

            // Add .xml and .m3u files
            std::string xmlPath = groupDir + "/" + group.name + ".xml";
            if (isFileTypeEnabled(xmlPath))
            {
                g_state->files[xmlPath] = std::make_shared<VirtualFile>(group.url + ".xml");
                Logger::Log(LogLevel::DEBUG, "Added .xml file: " + xmlPath);
            }

            std::string m3uPath = groupDir + "/" + group.name + ".m3u";
            if (isFileTypeEnabled(m3uPath))
            {
                g_state->files[m3uPath] = std::make_shared<VirtualFile>(group.url + ".m3u");
                Logger::Log(LogLevel::DEBUG, "Added .m3u file: " + m3uPath);
            }
            // Process sub-files in the group
            if (groupJson.contains("smfs") && groupJson["smfs"].is_array())
            {
                for (const auto &fileJson : groupJson["smfs"])
                {
                    SMFile smFile;
                    smFile.name = fileJson.value("name", "");
                    smFile.url = fileJson.value("url", "");

                    group.addSMFile(smFile);

                    std::string subDirPath = groupDir + "/" + smFile.name;
                    if (g_state->files.find(subDirPath) == g_state->files.end())
                    {
                        g_state->files[subDirPath] = nullptr; // Create subgroup directory
                        Logger::Log(LogLevel::DEBUG, "Added subgroup directory: " + subDirPath);
                    }

                    // Add .strm file
                    std::string strmPath = subDirPath + "/" + smFile.name + ".strm";
                    if (isFileTypeEnabled(strmPath))
                    {
                        g_state->files[strmPath] = std::make_shared<VirtualFile>(smFile.url);
                        Logger::Log(LogLevel::DEBUG, "Added .strm file: " + strmPath);
                    }

                    // Add .ts file
                    std::string tsPath = subDirPath + "/" + smFile.name + ".ts";
                    if (isFileTypeEnabled(tsPath))
                    {
                        g_state->files[tsPath] = std::make_shared<VirtualFile>(smFile.url);
                        Logger::Log(LogLevel::DEBUG, "Added .ts file: " + tsPath);
                    }
                }
            }

            groups[groupId] = group;
        }

        Logger::Log(LogLevel::INFO, "All groups processed successfully.");
    }
    catch (const std::exception &ex)
    {
        Logger::Log(LogLevel::ERROR, "JSON parse error: " + std::string(ex.what()));
    }
}

// End of api_client.cpp

// Start of logger.cpp
// File: logger.cpp
#include "logger.hpp"
#include <iostream>
#include <nlohmann/json.hpp>
#include <chrono>
#include <iomanip>
#include <sstream>

std::ofstream Logger::g_logFile;
std::mutex Logger::g_logMutex;
LogLevel Logger::currentLogLevel = LogLevel::INFO; // Default to INFO
bool Logger::setDebug = false;

std::string ToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::TRACE:
        return "TRACE";
    case LogLevel::DEBUG:
        return "DEBUG";
    case LogLevel::INFO:
        return "INFO";
    case LogLevel::WARN:
        return "WARN";
    case LogLevel::ERROR:
        return "ERROR";
    case LogLevel::FATAL:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

std::string GetCurrentTimestamp()
{
    auto now = std::chrono::system_clock::now();
    std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&nowTime), "%Y-%m-%d %H:%M:%S") << '.'
        << std::setfill('0') << std::setw(3) << nowMs.count();

    return oss.str();
}

void Logger::InitLogFile(const std::string &filePath)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open())
    {
        g_logFile.close();
    }
    g_logFile.open(filePath, std::ios::app);
    if (!g_logFile.good())
    {
        std::cerr << "[ERROR] Could not open log file: " << filePath << std::endl;
    }
}

void Logger::Log(LogLevel level, const std::string &msg)
{
    // Skip logs below the current log level
    if (level < currentLogLevel)
    {
        return;
    }
    if (setDebug)
    {
        std::cerr << "[" << ToString(level) << "] " << msg << std::endl;
    }

    std::lock_guard<std::mutex> lock(g_logMutex);

    nlohmann::json logJson;
    logJson["level"] = ToString(level);
    logJson["timestamp"] = GetCurrentTimestamp();
    logJson["message"] = msg;

    if (g_logFile.is_open())
    {
        g_logFile << logJson.dump() << std::endl;
    }
}

// End of logger.cpp

// Start of fuse_manager.cpp
// File: fuse_manager.cpp
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

// End of fuse_manager.cpp

// Start of fuse_operations.cpp
// File: fuse_operations.cpp
#include "fuse_operations.hpp"
#include "async_curl_client.hpp"
#include "stream_manager.hpp"
#include "logger.hpp"
#include <algorithm>
#include <set>
#include <chrono>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <iomanip>
#include <future>
#include <string.h>

// Map for path-to-inode mapping
std::unordered_map<std::string, fuse_ino_t> pathToInode;
std::unordered_map<fuse_ino_t, std::string> inodeToPath;
std::atomic<fuse_ino_t> nextInode{2}; // Start at 2, as 1 is reserved for the root inode

// Helper: Generate unique inodes
fuse_ino_t getInode(const std::string &path)
{
    Logger::Log(LogLevel::DEBUG, "getInode: Looking up inode for path: " + path);

    if (pathToInode.find(path) == pathToInode.end())
    {
        Logger::Log(LogLevel::TRACE, "getInode: Inode not found, creating new inode for path: " + path);
        pathToInode[path] = nextInode++;
        inodeToPath[pathToInode[path]] = path;
        Logger::Log(LogLevel::DEBUG, "getInode: Created inode " + std::to_string(pathToInode[path]) + " for path: " + path);
    }
    else
    {
        Logger::Log(LogLevel::TRACE, "getInode: Found existing inode " + std::to_string(pathToInode[path]) + " for path: " + path);
    }

    return pathToInode[path];
}

// Lookup callback
void fs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
    std::string parentPath = inodeToPath[parent];
    std::string path = parentPath + "/" + std::string(name);

    // Normalize path: Remove redundant slashes
    while (path.find("//") != std::string::npos)
    {
        path = path.replace(path.find("//"), 2, "/");
    }

    Logger::Log(LogLevel::DEBUG, "fs_lookup: Resolving parentPath: " + parentPath + "  path:  " + path);

    struct fuse_entry_param e = {};
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);

        // Search in in-memory files map
        auto it = g_state->files.find(path);
        if (it != g_state->files.end())
        {
            e.ino = getInode(path);
            e.attr.st_ino = e.ino;
            e.attr.st_mode = it->second ? S_IFREG | 0444 : S_IFDIR | 0755;
            e.attr.st_nlink = it->second ? 1 : 2;
            e.attr.st_size = it->second ? INT64_MAX : 0;

            Logger::Log(LogLevel::TRACE, "fs_lookup: Resolved inode attributes for path: " + path);
            fuse_reply_entry(req, &e);
            return;
        }
    }

    // Check cacheDir for the file
    std::string cachePath = g_state->cacheDir + path;
    struct stat st;
    if (lstat(cachePath.c_str(), &st) == 0)
    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);

        // Add to in-memory file map if not already present
        if (g_state->files.find(path) == g_state->files.end())
        {
            g_state->files[path] = std::make_shared<VirtualFile>(cachePath, st.st_size);
        }

        e.ino = getInode(path);
        e.attr.st_ino = e.ino;
        e.attr.st_mode = st.st_mode;
        e.attr.st_nlink = st.st_nlink;
        e.attr.st_size = st.st_size;
        e.attr.st_uid = st.st_uid;
        e.attr.st_gid = st.st_gid;
        e.attr.st_atime = st.st_atime;
        e.attr.st_mtime = st.st_mtime;
        e.attr.st_ctime = st.st_ctime;

        Logger::Log(LogLevel::DEBUG, "fs_lookup: Found file in cacheDir: " + cachePath);
        fuse_reply_entry(req, &e);
        return;
    }

    Logger::Log(LogLevel::ERROR, "fs_lookup: Path not found: " + path);
    fuse_reply_err(req, ENOENT);
}

// Getattr callback
void fs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
    Logger::Log(LogLevel::DEBUG, "fs_getattr: Inode: " + std::to_string(ino));

    struct stat st = {};
    if (ino == FUSE_ROOT_ID)
    {
        st.st_ino = FUSE_ROOT_ID;
        st.st_mode = S_IFDIR | 0755;
        st.st_nlink = 2;
        Logger::Log(LogLevel::DEBUG, "fs_getattr: Returning attributes for root directory.");
        fuse_reply_attr(req, &st, 1.0);
        return;
    }

    auto it = inodeToPath.find(ino);
    if (it == inodeToPath.end())
    {
        Logger::Log(LogLevel::ERROR, "fs_getattr: Inode not found: " + std::to_string(ino));
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string path = it->second;
    Logger::Log(LogLevel::DEBUG, "fs_getattr: Path resolved for inode: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto fileIt = g_state->files.find(path);
        if (fileIt != g_state->files.end())
        {
            st.st_ino = ino;
            st.st_mode = fileIt->second ? S_IFREG | 0444 : S_IFDIR | 0755;
            st.st_nlink = fileIt->second ? 1 : 2;
            st.st_size = fileIt->second ? INT64_MAX : 0;
            Logger::Log(LogLevel::DEBUG, "fs_getattr: Returning attributes for path: " + path);
            fuse_reply_attr(req, &st, 1.0);
            return;
        }
    }

    // Check cacheDir for the file
    std::string cachePath = g_state->cacheDir + path;
    if (lstat(cachePath.c_str(), &st) == 0)
    {
        st.st_ino = ino; // Assign the correct inode
        Logger::Log(LogLevel::DEBUG, "fs_getattr: Returning attributes from cacheDir for path: " + cachePath);
        fuse_reply_attr(req, &st, 1.0);
        return;
    }

    Logger::Log(LogLevel::ERROR, "fs_getattr: Path not found: " + path);
    fuse_reply_err(req, ENOENT);
}

// Readdir callback
void fs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Inode: " + std::to_string(ino));
    Logger::Log(LogLevel::TRACE, "fs_readdir: Offset: " + std::to_string(off));

    // Signal EOF for offsets greater than 0
    if (off > 0)
    {
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Offset > 0. No more entries to return.");
        fuse_reply_buf(req, nullptr, 0);
        return;
    }

    if (inodeToPath.find(ino) == inodeToPath.end())
    {
        Logger::Log(LogLevel::ERROR, "fs_readdir: Inode not found: " + std::to_string(ino));
        fuse_reply_err(req, ENOENT);
        return;
    }

    std::string parentPath = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_readdir: Parent path resolved: " + parentPath);

    if (parentPath.empty())
    {
        parentPath = "/";
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Parent Path was empty. Assuming root: /");
    }

    char *buf = (char *)calloc(1, size);
    size_t bufSize = 0;

    auto addDirEntry = [&](const std::string &name, fuse_ino_t inode, mode_t mode)
    {
        struct stat st = {};
        st.st_ino = inode;
        st.st_mode = mode;

        size_t entrySize = fuse_add_direntry(req, buf + bufSize, size - bufSize, name.c_str(), &st, bufSize + 1);
        if (entrySize == 0 || bufSize + entrySize > size)
        {
            Logger::Log(LogLevel::WARN, "fs_readdir: Buffer full or invalid entry: " + name);
            return false;
        }

        bufSize += entrySize;
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Added entry: " + name + ", inode: " + std::to_string(inode) +
                                         ", mode: " + std::to_string(mode) + ", buffer size: " + std::to_string(bufSize));
        return true;
    };

    addDirEntry(".", ino, S_IFDIR);
    addDirEntry("..", FUSE_ROOT_ID, S_IFDIR);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        Logger::Log(LogLevel::DEBUG, "fs_readdir: Processing entries for parent path: " + parentPath);

        for (const auto &kv : g_state->files)
        {
            if (kv.first.find(parentPath) == 0 && kv.first != parentPath)
            {
                std::string relativePath;

                // Handle special case where parentPath is "/"
                if (parentPath == "/")
                {
                    relativePath = kv.first;
                }
                else
                {
                    relativePath = kv.first.substr(parentPath.size());
                }

                // Ensure relativePath starts with a single '/'
                if (!relativePath.empty() && relativePath[0] == '/')
                {
                    relativePath = relativePath.substr(1);
                }
                else
                {
                    Logger::Log(LogLevel::TRACE, "fs_readdir: Skipping invalid relative path: " + relativePath);
                    continue;
                }

                // Ensure it's a direct child by checking for additional '/'
                if (relativePath.find('/') == std::string::npos)
                {
                    mode_t mode = kv.second ? S_IFREG : S_IFDIR;
                    Logger::Log(LogLevel::TRACE, "fs_readdir: Adding " + std::string(mode == S_IFDIR ? "directory" : "file") +
                                                     ": " + relativePath);
                    if (!addDirEntry(relativePath, getInode(kv.first), mode))
                    {
                        Logger::Log(LogLevel::WARN, "fs_readdir: Failed to add entry: " + relativePath);
                        break;
                    }
                }
                else
                {
                    Logger::Log(LogLevel::TRACE, "fs_readdir: Skipping non-direct child: " + relativePath);
                }
            }
        }
    }

    Logger::Log(LogLevel::DEBUG, "fs_readdir: Returning buffer of size: " + std::to_string(bufSize));
    fuse_reply_buf(req, buf, bufSize);
    free(buf);
}

// Open callback
void fs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_open: Inode: " + std::to_string(ino) + ", Path: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);

        if (it != g_state->files.end() && it->second)
        {
            auto vf = it->second.get();

            // Handle .ts files
            if (path.ends_with(".ts"))
            {
                if (!vf->streamContext)
                {
                    Logger::Log(LogLevel::DEBUG, "fs_open: Creating StreamManager for .ts file: " + path);
                    try
                    {
                        // Create an AsyncCurlClient instance
                        std::shared_ptr<IStreamingClient> asyncClient = std::make_shared<AsyncCurlClient>();

                        // Create and configure StreamManager
                        vf->streamContext = std::make_unique<StreamManager>(vf->url, 4 * 1024 * 1024, asyncClient, g_state->isShuttingDown);

                        // Start streaming in a controlled thread
                        vf->streamContext->startStreamingThread();

                        Logger::Log(LogLevel::DEBUG, "fs_open: StreamManager successfully created and started for: " + path);
                    }
                    catch (const std::exception &e)
                    {
                        Logger::Log(LogLevel::ERROR, "fs_open: Failed to create StreamManager for path: " + path + ". Error: " + e.what());
                        vf->streamContext.reset(); // Ensure no dangling pointer
                        fuse_reply_err(req, ENOMEM);
                        return;
                    }
                }
                else
                {
                    Logger::Log(LogLevel::DEBUG, "fs_open: Reusing existing StreamManager for: " + path);
                }

                vf->streamContext->incrementReaderCount(); // Increment reader count
            }

            // Pass the VirtualFile pointer to file handle
            fi->fh = reinterpret_cast<uint64_t>(vf);
            fuse_reply_open(req, fi);
            return;
        }
    }

    Logger::Log(LogLevel::ERROR, "fs_open: File not found: " + path);
    fuse_reply_err(req, ENOENT);
}

// Write callback
void fs_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_write: Writing " + std::to_string(size) + " bytes to " + path);

    // Redirect writes for external files to cacheDir
    std::string fullPath = g_state->cacheDir + path;
    Logger::Log(LogLevel::DEBUG, "fs_write: Redirecting write to: " + fullPath);

    int fd = open(fullPath.c_str(), O_WRONLY | O_CREAT, 0644);
    if (fd == -1)
    {
        fuse_reply_err(req, errno);
        return;
    }

    ssize_t res = pwrite(fd, buf, size, off);
    close(fd);

    if (res == -1)
    {
        fuse_reply_err(req, errno);
    }
    else
    {
        fuse_reply_write(req, res);
    }
}

// Release callback
void fs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
    std::lock_guard<std::mutex> lock(g_state->filesMutex);
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_release: Inode: " + std::to_string(ino) + ", Path: " + path);

    auto it = g_state->files.find(path);
    if (it != g_state->files.end() && it->second)
    {
        auto vf = it->second.get();
        if (vf->streamContext)
        {
            Logger::Log(LogLevel::DEBUG, "fs_release: Decrementing reader count for path: " + path);
            vf->streamContext->decrementReaderCount();

            if (vf->streamContext->isStopped())
            {
                Logger::Log(LogLevel::DEBUG, "fs_release: No more readers. Stopping stream: " + path);
                vf->streamContext->stopStreaming();
                vf->streamContext.reset(); // Release the StreamManager
            }
        }
    }

    Logger::Log(LogLevel::DEBUG, "fs_release: Inode: " + std::to_string(ino));
    fuse_reply_err(req, 0);
}

void fs_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi)
{
    (void)fi;
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_setattr: Modifying attributes for " + path);

    std::string fullPath = g_state->cacheDir + path;
    int res = 0;

    if (to_set & FUSE_SET_ATTR_MODE)
    {
        res = chmod(fullPath.c_str(), attr->st_mode);
        if (res == -1)
        {
            fuse_reply_err(req, errno);
            return;
        }
    }

    if (to_set & (FUSE_SET_ATTR_UID | FUSE_SET_ATTR_GID))
    {
        res = chown(fullPath.c_str(), attr->st_uid, attr->st_gid);
        if (res == -1)
        {
            fuse_reply_err(req, errno);
            return;
        }
    }

    // Fetch updated attributes
    struct stat st;
    res = lstat(fullPath.c_str(), &st);
    if (res == -1)
    {
        fuse_reply_err(req, errno);
        return;
    }

    fuse_reply_attr(req, &st, 1.0);
}

void fs_mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev)
{
    (void)rdev;
    std::string parentPath = inodeToPath[parent];
    std::string path = parentPath + "/" + name;

    Logger::Log(LogLevel::DEBUG, "fs_mknod: Creating file " + path);

    // Redirect file creation to cacheDir
    std::string fullPath = g_state->cacheDir + path;

    // Ensure parent directories exist
    size_t pos = 0;
    while ((pos = fullPath.find('/', pos + 1)) != std::string::npos)
    {
        std::string subDir = fullPath.substr(0, pos);
        if (mkdir(subDir.c_str(), 0755) == -1 && errno != EEXIST)
        {
            Logger::Log(LogLevel::ERROR, "fs_mknod: Failed to create directory " + subDir + ": " + strerror(errno));
            fuse_reply_err(req, errno);
            return;
        }
    }

    // Create the file
    int fd = open(fullPath.c_str(), O_CREAT | O_EXCL | O_WRONLY, mode);
    if (fd == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_mknod: Failed to create file " + fullPath + ": " + strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }
    close(fd);

    struct stat st;
    if (lstat(fullPath.c_str(), &st) == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_mknod: Failed to stat file " + fullPath + ": " + strerror(errno));
        fuse_reply_err(req, errno);
        return;
    }

    struct fuse_entry_param e = {};
    e.ino = getInode(path);
    e.attr = st;
    e.attr_timeout = 1.0;
    e.entry_timeout = 1.0;

    Logger::Log(LogLevel::DEBUG, "fs_mknod: File created successfully at " + fullPath);
    fuse_reply_entry(req, &e);
}

// Opendir callback
void fs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
    Logger::Log(LogLevel::DEBUG, "fs_opendir: Inode: " + std::to_string(ino));
    fuse_reply_open(req, fi);
}

// Releasedir callback
void fs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi)
{
    (void)fi;
    Logger::Log(LogLevel::DEBUG, "fs_releasedir: Inode: " + std::to_string(ino));
    fuse_reply_err(req, 0);
}

void fs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size)
{
    (void)size;
    Logger::Log(LogLevel::DEBUG, "fs_getxattr: Inode: " + std::to_string(ino) + ", Name: " + std::string(name));

    // Extended attributes are not implemented
    fuse_reply_err(req, ENOTSUP);
}

void fs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi)
{
    (void)fi;
    std::string path = inodeToPath[ino];
    Logger::Log(LogLevel::DEBUG, "fs_read: Inode: " + std::to_string(ino) + ", Path: " + path);

    {
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        auto it = g_state->files.find(path);

        if (it != g_state->files.end() && it->second)
        {
            auto vf = it->second.get();
            StreamManager *streamManager = vf->streamContext.get();

            // Handle virtual files (.ts)
            if (path.ends_with(".ts"))
            {
                if (!streamManager)
                {
                    Logger::Log(LogLevel::ERROR, "fs_read: StreamManager not found for virtual file: " + path);
                    fuse_reply_err(req, ENOENT);
                    return;
                }

                char *buf = new char[size];
                size_t bytesRead = streamManager->getPipe().read(buf, size, g_state->isShuttingDown);

                Logger::Log(LogLevel::TRACE, "fs_read: Virtual file read returned " + std::to_string(bytesRead) + " bytes for path: " + path);
                fuse_reply_buf(req, buf, bytesRead);
                delete[] buf;
                return;
            }

            // Handle other virtual files (.strm, .xml, .m3u)
            if (path.ends_with(".strm"))
            {
                // Return the contentUrl as plain text
                std::string contentUrl = vf->url;
                Logger::Log(LogLevel::DEBUG, "fs_read: Returning contentUrl for .strm file: " + contentUrl);

                size_t toRead = std::min(size, contentUrl.size() - static_cast<size_t>(off));
                if (static_cast<size_t>(off) >= contentUrl.size())
                {
                    fuse_reply_buf(req, nullptr, 0); // EOF
                }
                else
                {
                    fuse_reply_buf(req, contentUrl.data() + off, toRead);
                }
                return;
            }

            if (path.ends_with(".xml") || path.ends_with(".m3u"))
            {
                std::string contentUrl = vf->url;
                if (path.ends_with(".xml"))
                    contentUrl += ".xml";
                else if (path.ends_with(".m3u"))
                    contentUrl += ".m3u";

                Logger::Log(LogLevel::DEBUG, "fs_read: Fetching content from URL: " + contentUrl);

                char *buf = new char[size];
                size_t bytesRead = streamManager->readContent(contentUrl, buf, size, off);
                fuse_reply_buf(req, buf, bytesRead);
                delete[] buf;
                return;
            }
        }
    }

    // Handle physical files in the cache directory
    std::string cachePath = g_state->cacheDir + path;
    Logger::Log(LogLevel::DEBUG, "fs_read: Falling back to cacheDir for file: " + cachePath);

    int fd = open(cachePath.c_str(), O_RDONLY);
    if (fd == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_read: File not found in cacheDir: " + cachePath);
        fuse_reply_err(req, ENOENT);
        return;
    }

    char *buf = new char[size];
    ssize_t res = pread(fd, buf, size, off);
    close(fd);

    if (res == -1)
    {
        Logger::Log(LogLevel::ERROR, "fs_read: Error reading file in cacheDir: " + cachePath);
        delete[] buf;
        fuse_reply_err(req, errno);
    }
    else
    {
        Logger::Log(LogLevel::DEBUG, "fs_read: Read " + std::to_string(res) + " bytes from cacheDir: " + cachePath);
        fuse_reply_buf(req, buf, res);
        delete[] buf;
    }
}

// End of fuse_operations.cpp

// Start of main.cpp
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
    std::string host = "10.3.10.50";
    std::string port = "7095";
    std::string apiKey = "f4bed758a1aa45a38c801ed6893d70fb";
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
                      << "--mount <mountpoint>            Set the FUSE mount point\n"
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

    // Fetch initial file list
    g_state->apiClient.fetchFileList();

    // Start WebSocket client
    WebSocketClient wsClient(host, port, apiKey);
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

    Logger::Log(LogLevel::INFO, "Stopping WebSocket client...");
    wsClient.Stop();
    if (wsThread.joinable())
    {
        wsThread.join();
    }

    Logger::Log(LogLevel::INFO, "SMFS exited cleanly.");
    return 0;
}

// End of main.cpp

// Start of stream_manager.cpp
#include "stream_manager.hpp"
#include "logger.hpp"
#include <curl/curl.h>
#include <cstring>
#include <algorithm>
#include <stdexcept>
#include <thread>
#include <stop_token>
#include <future>

StreamManager::StreamManager(const std::string &url, size_t bufferCapacity, std::shared_ptr<IStreamingClient> client, std::atomic<bool> &shutdownFlag)
    : url_(url), pipe_(bufferCapacity), client_(std::move(client)), isShuttingDown_(shutdownFlag) {}

void StreamManager::incrementReaderCount()
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++readerCount_;
    stopRequested_ = false;
    Logger::Log(LogLevel::DEBUG, "StreamManager::incrementReaderCount: Reader count increased to " + std::to_string(readerCount_));
}

void StreamManager::decrementReaderCount()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (--readerCount_ <= 0)
    {
        Logger::Log(LogLevel::DEBUG, "StreamManager::decrementReaderCount: No readers left, stopping stream.");
        stopStreaming();
    }
}

void StreamManager::startStreaming()
{
    Logger::Log(LogLevel::INFO, "StreamManager::startStreaming: Starting stream for URL: " + url_);
    client_->fetchStreamAsync(url_, [this](const std::string &data)
                              {
        if (!pipe_.write(data.data(), data.size(), stopRequested_))
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::startStreaming: Failed to write data to pipe.");
        } });
}

void StreamManager::stopStreaming()
{
    Logger::Log(LogLevel::INFO, "StreamManager::stopStreaming: Stopping stream for URL: " + url_);
    stopRequested_ = true;
}

void StreamManager::startStreamingThread()
{
    streamingThread_ = std::jthread([this](std::stop_token stopToken)
                                    {
        (void)stopToken;
        
        try
        {
            streamingThreadFunc();
        }
        catch (const std::exception &e)
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::startStreamingThread: Exception occurred: " + std::string(e.what()));
        } });
}

void StreamManager::stopStreamingThread()
{
    if (streamingThread_.joinable())
    {
        Logger::Log(LogLevel::INFO, "StreamManager::stopStreamingThread: Requesting thread stop for URL: " + url_);
        streamingThread_.request_stop();
        streamingThread_.join();
    }
}

const std::string &StreamManager::getUrl() const
{
    return url_;
}

Pipe &StreamManager::getPipe()
{
    return pipe_;
}

bool StreamManager::isStopped() const
{
    return stopRequested_;
}

size_t StreamManager::readContent(const std::string &toFetchUrl, char *buf, size_t size, off_t offset)
{
    return fetchUrlContent(toFetchUrl, buf, size, offset);
}

StreamManager::~StreamManager()
{
    Logger::Log(LogLevel::INFO, "StreamManager::~StreamManager: Cleaning up StreamManager for URL: " + url_);
    stopStreamingThread();
}

size_t StreamManager::WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t totalSize = size * nmemb;
    std::string *str = static_cast<std::string *>(userp);
    str->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}

size_t StreamManager::writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *manager = reinterpret_cast<StreamManager *>(userdata);
    size_t total = size * nmemb;

    if (manager->stopRequested_)
    {
        Logger::Log(LogLevel::INFO, "StreamManager::writeCallback: Stop requested. Exiting write.");
        return 0; // Inform CURL to stop
    }

    if (!manager->pipe_.write(ptr, total, manager->stopRequested_))
    {
        if (manager->stopRequested_)
        {
            Logger::Log(LogLevel::INFO, "StreamManager::writeCallback: Write aborted due to stop request.");
            return 0; // Stop without logging an error
        }

        Logger::Log(LogLevel::ERROR, "StreamManager::writeCallback: Failed to write to pipe.");
        return 0; // Inform CURL of failure
    }

    Logger::Log(LogLevel::DEBUG, "StreamManager::writeCallback: Wrote " + std::to_string(total) + " bytes to pipe.");
    return total;
}

size_t StreamManager::fetchUrlContent(const std::string &url, char *buf, size_t size, off_t offset)
{
    CURL *curl = curl_easy_init();
    if (!curl)
    {
        Logger::Log(LogLevel::ERROR, "StreamManager::fetchUrlContent: Failed to initialize CURL.");
        return 0;
    }

    CURLcode res;
    std::string retrievedData;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &retrievedData);

    res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
        Logger::Log(LogLevel::ERROR, "StreamManager::fetchUrlContent: CURL error: " + std::string(curl_easy_strerror(res)));
        return 0;
    }

    if (static_cast<std::make_unsigned_t<off_t>>(offset) >= retrievedData.size())
    {
        return 0;
    }

    size_t toRead = std::min(size, retrievedData.size() - offset);
    memcpy(buf, retrievedData.data() + offset, toRead);
    return toRead;
}

void StreamManager::streamingThreadFunc()
{
    Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Starting stream for URL: " + url_);

    CURL *curl = curl_easy_init();
    if (!curl)
    {
        Logger::Log(LogLevel::ERROR, "StreamManager::streamingThreadFunc: Failed to initialize CURL.");
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 100000L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);

    while (!stopRequested_ && !isShuttingDown_.load())
    {
        Logger::Log(LogLevel::DEBUG, "StreamManager::streamingThreadFunc: Attempting stream for URL: " + url_);
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_ABORTED_BY_CALLBACK && stopRequested_)
        {
            Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Stream stopped by request for URL: " + url_);
            break;
        }
        else if (res != CURLE_OK)
        {
            Logger::Log(LogLevel::ERROR, "StreamManager::streamingThreadFunc: CURL error: " + std::string(curl_easy_strerror(res)));
            if (isShuttingDown_.load())
            {
                Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Exiting due to shutdown for URL: " + url_);
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        else
        {
            Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Stream completed successfully for URL: " + url_);
            break;
        }
    }

    curl_easy_cleanup(curl);
    Logger::Log(LogLevel::INFO, "StreamManager::streamingThreadFunc: Exiting for URL: " + url_);
}

// End of stream_manager.cpp

// Start of stream_manager.hpp
#pragma once
#include "pipe.hpp"
#include "i_streaming_client.hpp"
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include <memory>

class StreamManager
{
public:
    explicit StreamManager(const std::string &url, size_t bufferCapacity, std::shared_ptr<IStreamingClient> client, std::atomic<bool> &shutdownFlag);

    void incrementReaderCount();
    void decrementReaderCount();

    void startStreaming();
    void stopStreaming();
    void startStreamingThread();
    void stopStreamingThread();

    const std::string &getUrl() const;
    Pipe &getPipe();
    bool isStopped() const;

    size_t readContent(const std::string &toFetchUrl, char *buf, size_t size, off_t offset);

    ~StreamManager();

private:
    static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata);
    static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp);

    size_t fetchUrlContent(const std::string &url, char *buf, size_t size, off_t offset);
    void streamingThreadFunc();

    std::string url_;
    Pipe pipe_;
    std::shared_ptr<IStreamingClient> client_;
    std::jthread streamingThread_;
    std::atomic<int> readerCount_{0};
    std::atomic<bool> stopRequested_{false};
    std::mutex mutex_;
    std::atomic<bool> &isShuttingDown_;
};

// End of stream_manager.hpp

// Start of fuse_operations.hpp
// File: fuse_operations.hpp
#pragma once
#define FUSE_USE_VERSION 35

#include <fuse3/fuse_lowlevel.h> // For low-level FUSE operations
#include <unordered_map>
#include <string>
#include <atomic>
#include <memory>
#include <mutex>
#include "stream_manager.hpp"
#include <smfs_state.hpp>

// Global state
extern std::unordered_map<std::string, fuse_ino_t> pathToInode;
extern std::unordered_map<fuse_ino_t, std::string> inodeToPath;
extern std::atomic<fuse_ino_t> nextInode;

// FUSE operation callbacks
void fs_lookup(fuse_req_t req, fuse_ino_t parent, const char *name);
void fs_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);
void fs_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi);

// Optional FUSE callbacks
void fs_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi);
void fs_release(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_opendir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_releasedir(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi);
void fs_setattr(fuse_req_t req, fuse_ino_t ino, struct stat *attr, int to_set, struct fuse_file_info *fi);
void fs_mknod(fuse_req_t req, fuse_ino_t parent, const char *name, mode_t mode, dev_t rdev);
void fs_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name, size_t size);
// Helper function
fuse_ino_t getInode(const std::string &path);

// End of fuse_operations.hpp

// Start of async_curl_client.hpp
#pragma once

#include "i_streaming_client.hpp"
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <map>

class AsyncCurlClient : public IStreamingClient
{
public:
    AsyncCurlClient();
    ~AsyncCurlClient();

    void fetchStreamAsync(const std::string &url, std::function<void(const std::string &data)> onDataReceived) override;

private:
    CURLM *multiHandle_;
    std::thread workerThread_;
    std::atomic<bool> isRunning_{true};
    std::map<CURL *, std::function<void()>> callbacks_;
    std::mutex mutex_;

    static size_t writeCallback(void *contents, size_t size, size_t nmemb, void *userp);
    void eventLoop();
};

// End of async_curl_client.hpp

// Start of smfs_state.hpp
// File: smfs_state.hpp
#pragma once

#include <map>
#include <mutex>
#include <memory>
#include "api_client.hpp"
#include <atomic>
#include <thread>
#include <curl/curl.h>
#include <deque>
#include <condition_variable>
#include <set>
#include "stream_manager.hpp"

// Forward declare so we can reference below
struct StreamContext;

// The "file" in FUSE
struct VirtualFile
{
    std::string url;

    // A StreamManager pointer for indefinite streaming
    std::unique_ptr<StreamManager> streamContext;

    bool isUserFile = false;
    mode_t st_mode = 0111; // default
    uid_t st_uid = 0;      // optional
    gid_t st_gid = 0;      // optional
    std::shared_ptr<std::vector<char>> content;

    // Default constructor
    VirtualFile() = default;

    // Constructor for URL only
    explicit VirtualFile(const std::string &u)
        : url(u) {}

    // Constructor for URL and other attributes
    VirtualFile(const std::string &u, mode_t mode, uid_t uid, gid_t gid, bool isUser = false)
        : url(u), isUserFile(isUser), st_mode(mode), st_uid(uid), st_gid(gid) {}

    // Constructor for URL and size
    VirtualFile(const std::string &u, long int size)
        : url(u)
    {
        // Initialize `content` if size > 0
        if (size > 0)
        {
            content = std::make_shared<std::vector<char>>(size);
        }
    }

    // No copy
    VirtualFile(const VirtualFile &) = delete;
    VirtualFile &operator=(const VirtualFile &) = delete;

    // Move
    VirtualFile(VirtualFile &&) = default;
    VirtualFile &operator=(VirtualFile &&) = default;
};

// SMFS = "Stream Master File System"
struct SMFS
{
    std::atomic<bool> isShuttingDown{false};
    std::set<std::string> enabledFileTypes;
    std::string cacheDir;
    // Map of path -> VirtualFile (or nullptr if directory)
    std::map<std::string, std::shared_ptr<VirtualFile>> files;
    std::mutex filesMutex;

    APIClient apiClient;

    SMFS(const std::string &host,
         const std::string &port,
         const std::string &apiKey,
         const std::string &streamGroupProfileIds = "0",
         bool isShort = true)
        : apiClient(host, port, apiKey, streamGroupProfileIds, isShort)
    {
    }

    // Non-copyable
    SMFS(const SMFS &) = delete;
    SMFS &operator=(const SMFS &) = delete;
};

extern std::unique_ptr<SMFS> g_state;

// End of smfs_state.hpp

// Start of api_client.hpp
// File: api_client.hpp
#pragma once

#include <string>
#include <map>
#include "sgfs.hpp"

class APIClient
{
public:
    APIClient(const std::string &host,
              const std::string &port,
              const std::string &apiKey,
              const std::string &streamGroupProfileIds = "0",
              bool isShort = true);

    void fetchFileList();

    const std::map<int, SGFS> &getGroups() const;

private:
    std::string baseUrl;
    std::map<int, SGFS> groups;
    void processResponse(const std::string &response);
};

// End of api_client.hpp

// Start of i_streaming_client.hpp
#pragma once
#include <string>
#include <functional>

class IStreamingClient
{
public:
    virtual void fetchStreamAsync(const std::string &url, std::function<void(const std::string &data)> onDataReceived) = 0;
    virtual ~IStreamingClient() = default;
};

// End of i_streaming_client.hpp

// Start of fuse_manager.hpp
// File: fuse_manager.hpp
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

// End of fuse_manager.hpp

// Start of websocket_client.hpp
// File: websocket_client.hpp
#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

namespace beast = boost::beast; // From <boost/beast.hpp>
namespace websocket = beast::websocket;
namespace net = boost::asio; // From <boost/asio.hpp>
using tcp = net::ip::tcp;

class WebSocketClient
{
public:
    WebSocketClient(const std::string &host, const std::string &port, const std::string &apiKey);
    ~WebSocketClient();

    void Start();
    void Stop();

private:
    void ConnectAndListen();
    void HandleMessage(const std::string &message);

    std::string host_;
    std::string port_;
    std::string apiKey_;
    std::atomic<bool> shouldRun{true};
    std::thread wsThread;
};

// Constructor
inline WebSocketClient::WebSocketClient(const std::string &host, const std::string &port, const std::string &apiKey)
    : host_(host), port_(port), apiKey_(apiKey)
{
}

// Destructor - Ensures proper cleanup
inline WebSocketClient::~WebSocketClient()
{
    Stop();
    if (wsThread.joinable())
    {
        wsThread.join();
    }
}

// Start the WebSocket client in a new thread
inline void WebSocketClient::Start()
{
    std::cout << "[DEBUG] WebSocketClient::Start() called." << std::endl;

    wsThread = std::thread([this]()
                           {
        std::cout << "[DEBUG] WebSocketClient thread started." << std::endl;
        ConnectAndListen(); });
}

void WebSocketClient::Stop()
{
    Logger::Log(LogLevel::DEBUG, "WebSocketClient::Stop() called.");
    if (!shouldRun.exchange(false))
    {
        Logger::Log(LogLevel::WARN, "WebSocket client is already stopped.");
        return;
    }

    auto start = std::chrono::steady_clock::now();

    // Wait for the thread to exit
    while (wsThread.joinable())
    {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5))
        {
            Logger::Log(LogLevel::WARN, "WebSocket thread did not exit in time. Forcing shutdown...");
            pthread_cancel(wsThread.native_handle());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (wsThread.joinable())
    {
        try
        {
            wsThread.join();
        }
        catch (const std::system_error &e)
        {
            Logger::Log(LogLevel::ERROR, "Error joining WebSocket thread: " + std::string(e.what()));
        }
    }

    Logger::Log(LogLevel::INFO, "WebSocket client stopped.");
}

void WebSocketClient::ConnectAndListen()
{
    int retryDelay = 1; // Start with a 1-second delay

    while (shouldRun)
    {
        try
        {
            net::io_context ioc;
            tcp::resolver resolver{ioc};
            auto const results = resolver.resolve(host_, port_);
            websocket::stream<beast::tcp_stream> ws{ioc};

            // Attempt to connect
            Logger::Log(LogLevel::INFO, "Attempting WebSocket connection...");
            ws.next_layer().connect(*results.begin());
            ws.handshake(host_, "/ws");
            Logger::Log(LogLevel::INFO, "WebSocket connection established.");
            // Fetch the file list after reconnecting
            if (g_state != nullptr)
            {
                Logger::Log(LogLevel::INFO, "Fetching file list after reconnecting...");
                g_state->apiClient.fetchFileList();
                Logger::Log(LogLevel::INFO, "File list fetched successfully after reconnecting.");
            }
            beast::flat_buffer buffer;
            while (shouldRun)
            {
                try
                {
                    // Read messages
                    ws.read(buffer);
                    HandleMessage(beast::buffers_to_string(buffer.data()));
                    buffer.consume(buffer.size());
                    retryDelay = 1; // Reset delay on successful read
                }
                catch (const beast::system_error &e)
                {
                    if (e.code() == websocket::error::closed)
                    {
                        Logger::Log(LogLevel::WARN, "WebSocket closed by server.");
                        break;
                    }
                    throw; // Rethrow other exceptions
                }
            }

            // Cleanly close the WebSocket
            if (shouldRun)
            {
                Logger::Log(LogLevel::INFO, "Closing WebSocket connection...");
                ws.close(websocket::close_code::normal);
            }
        }
        catch (const std::exception &e)
        {
            Logger::Log(LogLevel::ERROR, "WebSocket connection failed: " + std::string(e.what()));
        }

        if (!shouldRun)
            break;

        // Reconnect with exponential backoff
        Logger::Log(LogLevel::INFO, "Retrying connection in " + std::to_string(retryDelay) + " seconds.");
        std::this_thread::sleep_for(std::chrono::seconds(retryDelay));
        retryDelay = std::min(retryDelay * 2, 32); // Exponential backoff up to 32 seconds
    }

    Logger::Log(LogLevel::INFO, "WebSocketClient::ConnectAndListen exiting.");
}

// Handle incoming messages
void WebSocketClient::HandleMessage(const std::string &message)
{
    Logger::Log(LogLevel::DEBUG, "Received message: " + message);

    if (message == "reload")
    {
        Logger::Log(LogLevel::INFO, "Reload command received. Fetching file list...");
        g_state->apiClient.fetchFileList();
        Logger::Log(LogLevel::INFO, "File list reloaded.");
    }
    else if (message.starts_with("delete:"))
    {
        std::string filePath = message.substr(7);
        Logger::Log(LogLevel::INFO, "Delete command received for file: " + filePath);
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        g_state->files.erase(filePath);
    }
    // Add additional command handling here
}

// End of websocket_client.hpp

// Start of logger.hpp
// File: logger.hpp
#pragma once
#include <string>
#include <fstream>
#include <mutex>

enum class LogLevel
{
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

class Logger
{
public:
    static void InitLogFile(const std::string &filePath);
    static void Log(LogLevel level, const std::string &msg);

    // Set log level
    static void SetLogLevel(LogLevel level)
    {
        currentLogLevel = level;
    }

    static void SetDebug(bool debug)
    {
        setDebug = debug;
    }

private:
    static std::ofstream g_logFile;
    static std::mutex g_logMutex;
    static LogLevel currentLogLevel;
    static bool setDebug;
};

// End of logger.hpp

// Start of sgfs.hpp
// File: sgfs.hpp
#pragma once
#include <string>
#include <vector>

class SMFile
{
public:
    std::string name;
    std::string url;

    SMFile() = default;
    SMFile(const std::string &n, const std::string &u)
        : name(n), url(u) {}
};

class SGFS
{
public:
    std::string name;
    std::string url;
    std::vector<SMFile> smFiles;

    SGFS() = default;
    SGFS(const std::string &n, const std::string &u)
        : name(n), url(u) {}

    void addSMFile(const SMFile &file)
    {
        smFiles.push_back(file);
    }
};

// End of sgfs.hpp

// Start of pipe.hpp
// File: pipe.hpp
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include "logger.hpp"

class Pipe
{
public:
    explicit Pipe(size_t capacity) : capacity_(capacity) {}

    // Producer writes data into the pipe
    bool write(const char *data, size_t len, std::atomic<bool> &stop)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t written = 0;

        while (written < len && !stop.load())
        {
            while (queue_.size() >= capacity_ && !stop.load())
                condNotFull_.wait(lock);

            if (stop.load())
                return false;

            size_t batchSize = std::min(capacity_ - queue_.size(), len - written);
            for (size_t i = 0; i < batchSize; ++i)
                queue_.push(data[written + i]);

            written += batchSize;
            condNotEmpty_.notify_one();
        }

        return true;
    }

    // Consumer reads data from the pipe
    size_t read(char *dest, size_t len, std::atomic<bool> &stop)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        size_t bytesRead = 0;

        while (bytesRead < len)
        {
            if (stop.load() && queue_.empty())
            {
                Logger::Log(LogLevel::TRACE, "Pipe::read: Returning EOF. Pipe is empty and stop is requested.");
                break;
            }

            if (!queue_.empty())
            {
                dest[bytesRead++] = queue_.front();
                queue_.pop();
                condNotFull_.notify_one();
            }
            else
            {
                Logger::Log(LogLevel::TRACE, "Pipe::read: Waiting for data in the queue.");
                condNotEmpty_.wait(lock, [&]
                                   { return !queue_.empty() || stop.load(); });
            }
        }

        Logger::Log(LogLevel::TRACE, "Pipe::read: Read " + std::to_string(bytesRead) + " bytes. Requested: " + std::to_string(len));
        if (bytesRead > 0)
        {
            // Logger::Log(LogLevel::DEBUG, "Pipe::read: Read " + std::to_string(bytesRead) + " bytes. Buffer filled partially/completely.");
        }
        else
        {
            Logger::Log(LogLevel::TRACE, "Pipe::read: Returning 0 bytes. Pipe is empty.");
        }
        return bytesRead;
    }

private:
    std::queue<char> queue_;
    size_t capacity_;
    std::mutex mutex_;
    std::condition_variable condNotEmpty_;
    std::condition_variable condNotFull_;
};

// End of pipe.hpp

