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
