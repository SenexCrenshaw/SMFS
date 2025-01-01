// websocket_client.hpp
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
    if (!shouldRun.exchange(false)) // Only proceed if not already stopped
        return;

    Logger::Log(LogLevel::INFO, "Stopping WebSocket client...");
    // Perform additional cleanup if needed
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
            ws.next_layer().connect(*results.begin());
            ws.handshake(host_, "/ws");

            Logger::Log(LogLevel::INFO, "WebSocket connection established.");

            beast::flat_buffer buffer;
            while (shouldRun)
            {
                try
                {
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
        }
        catch (const std::exception &e)
        {
            Logger::Log(LogLevel::ERROR, "WebSocket connection failed: " + std::string(e.what()));
        }

        if (!shouldRun)
            break;

        Logger::Log(LogLevel::INFO, "Retrying connection in " + std::to_string(retryDelay) + " seconds.");
        std::this_thread::sleep_for(std::chrono::seconds(retryDelay));
        retryDelay = std::min(retryDelay * 2, 32); // Exponential backoff up to 32 seconds
    }
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
