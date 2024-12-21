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
};
