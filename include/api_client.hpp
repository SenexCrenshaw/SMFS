#pragma once
#include <string>
#include <map>

class APIClient
{
public:
    APIClient(const std::string &host, const std::string &port, const std::string &apiKey);

    void fetchFileList();
    const std::map<std::string, std::string> &getFiles() const { return files; }

private:
    std::string baseUrl;
    std::map<std::string, std::string> files;
};
