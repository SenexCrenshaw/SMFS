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

    CURL *curl = curl_easy_init();
    std::string response;
    if (!curl)
    {
        Logger::Log(LogLevel::ERROR, "Failed to initialize CURL.");
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, baseUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK)
    {
        Logger::Log(LogLevel::ERROR,
                    "CURL error: " + std::string(curl_easy_strerror(res)));
        curl_easy_cleanup(curl);
        return;
    }

    Logger::Log(LogLevel::INFO, "File list fetched successfully.");
    curl_easy_cleanup(curl);

    try
    {
        auto jsonResponse = json::parse(response);
        groups.clear();

        // Lock the files map
        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        g_state->files.clear();

        // The structure might be:
        // {
        //   "2": {
        //     "name": "Test",
        //     "url":  "...",
        //     "smfs": [
        //       {"name": "Game Show Network", "url": "..."},
        //       {"name": "Cinemax US",       "url": "..."}
        //     ]
        //   }
        // }

        for (auto &entry : jsonResponse.items())
        {
            int groupId = std::stoi(entry.key());
            const auto &groupJson = entry.value();

            SGFS group;
            group.name = groupJson.value("name", "");
            group.url = groupJson.value("url", "");

            // e.g. "/Test"
            std::string groupDir = "/" + group.name;
            g_state->files[groupDir] = nullptr; // directory
            Logger::Log(LogLevel::DEBUG, "Created group directory: " + groupDir);
            // .xml
            std::string xmlPath = groupDir + "/" + group.name + ".xml";
            g_state->files[xmlPath] = std::make_shared<VirtualFile>(VirtualFile(group.url));
            Logger::Log(LogLevel::DEBUG, "Added file: " + xmlPath);
            // .m3u
            std::string m3uPath = groupDir + "/" + group.name + ".m3u";
            g_state->files[m3uPath] = std::make_shared<VirtualFile>(VirtualFile(group.url));
            Logger::Log(LogLevel::DEBUG, "Added file: " + m3uPath);

            // If "smfs" is an array
            if (groupJson.contains("smfs") && groupJson["smfs"].is_array())
            {

                for (auto &fileJson : groupJson["smfs"])
                {
                    SMFile smFile;
                    smFile.name = fileJson.value("name", "");
                    smFile.url = fileJson.value("url", "");

                    group.addSMFile(smFile);
                    // For each channel subdirectory
                    // e.g. "/Test/HBO"
                    std::string subDirPath = groupDir + "/" + smFile.name;
                    g_state->files[subDirPath] = nullptr; // directory
                    Logger::Log(LogLevel::DEBUG, "Added Directory: " + subDirPath);
                    // .ts
                    std::string tsPath = subDirPath + "/" + smFile.name + ".ts";
                    g_state->files[tsPath] = std::make_shared<VirtualFile>(VirtualFile(smFile.url));
                    Logger::Log(LogLevel::DEBUG, "Added file: " + tsPath);
                    // .strm
                    std::string strmPath = subDirPath + "/" + smFile.name + ".strm";
                    g_state->files[strmPath] = std::make_shared<VirtualFile>(VirtualFile(smFile.url));
                    Logger::Log(LogLevel::DEBUG, "Added file: " + strmPath);
                }
            }

            groups[groupId] = group;
        }

        Logger::Log(LogLevel::INFO, "All groups processed successfully.");
    }
    catch (const std::exception &ex)
    {
        Logger::Log(LogLevel::ERROR,
                    "JSON parse error: " + std::string(ex.what()));
    }
}
