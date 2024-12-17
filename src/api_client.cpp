#include "api_client.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include "logger.hpp"

using json = nlohmann::json;

// Callback to write response data
size_t write_response(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *response = static_cast<std::string *>(userdata);
    response->append(static_cast<char *>(ptr), size * nmemb);
    return size * nmemb;
}

// Constructor: Constructs the base URL
APIClient::APIClient(const std::string &host, const std::string &port, const std::string &apiKey)
{
    baseUrl = "http://" + host + ":" + port + "/api/files/getsmfs?apiKey=" + apiKey + "&isShort=true";
}

// Fetch file list from the API
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
        Logger::Log(LogLevel::ERROR, "CURL error: " + std::string(curl_easy_strerror(res)));
    }
    else
    {
        Logger::Log(LogLevel::INFO, "File list fetched successfully.");
        try
        {
            auto jsonResponse = json::parse(response);
            for (const auto &file : jsonResponse["dirSMFSFiles"]["Test"])
            {
                std::string name = "/" + file["name"].get<std::string>() + ".ts";
                std::string url = file["url"].get<std::string>();
                files[name] = url;
                Logger::Log(LogLevel::DEBUG, "File: " + name + ", URL: " + url);
            }
        }
        catch (const std::exception &e)
        {
            Logger::Log(LogLevel::ERROR, "JSON parse error: " + std::string(e.what()));
        }
    }

    curl_easy_cleanup(curl);
}
