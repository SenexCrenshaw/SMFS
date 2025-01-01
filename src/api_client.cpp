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
        auto jsonResponse = json::parse(response);
        groups.clear();

        std::lock_guard<std::mutex> lock(g_state->filesMutex);
        g_state->files.clear();

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
            g_state->files[xmlPath] = std::make_shared<VirtualFile>(group.url + ".xml");
            Logger::Log(LogLevel::DEBUG, "Added .xml file: " + xmlPath);

            std::string m3uPath = groupDir + "/" + group.name + ".m3u";
            g_state->files[m3uPath] = std::make_shared<VirtualFile>(group.url + ".m3u");
            Logger::Log(LogLevel::DEBUG, "Added .m3u file: " + m3uPath);

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
                    else
                    {
                        Logger::Log(LogLevel::WARN, "Subgroup directory already exists: " + subDirPath);
                    }

                    // Add .strm file
                    std::string strmPath = subDirPath + "/" + smFile.name + ".strm";
                    g_state->files[strmPath] = std::make_shared<VirtualFile>(smFile.url);
                    Logger::Log(LogLevel::DEBUG, "Added .strm file: " + strmPath);

                    // Add .ts file
                    std::string tsPath = subDirPath + "/" + smFile.name + ".ts";
                    if (g_state->files.find(tsPath) == g_state->files.end())
                    {
                        g_state->files[tsPath] = std::make_shared<VirtualFile>(smFile.url);
                        Logger::Log(LogLevel::DEBUG, "Added .ts file: " + tsPath);
                    }
                    else
                    {
                        Logger::Log(LogLevel::ERROR, "Conflict: .ts file already exists for path: " + tsPath);
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
