#pragma once
#include <string>
#include <functional>

class IStreamingClient
{
public:
    virtual void fetchStreamAsync(const std::string &url, std::function<void(const std::string &data)> onDataReceived) = 0;
    virtual ~IStreamingClient() = default;
};
