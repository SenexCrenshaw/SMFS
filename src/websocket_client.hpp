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

// Stop the WebSocket client
inline void WebSocketClient::Stop()
{
    shouldRun = false;
    std::cout << "[INFO] Stopping WebSocket client..." << std::endl;
}

// Connect to the WebSocket server and listen for messages
inline void WebSocketClient::ConnectAndListen()
{
    while (shouldRun)
    {
        try
        {
            net::io_context ioc;

            // Resolve the host and port
            tcp::resolver resolver{ioc};
            auto const results = resolver.resolve(host_, port_);

            // Create and connect the WebSocket stream
            websocket::stream<beast::tcp_stream> ws{ioc};
            ws.next_layer().connect(*results.begin());
            std::cout << "[INFO] Connection to server established." << std::endl;

            // Perform the WebSocket handshake
            std::string target = "/ws";
            ws.handshake(host_, target);
            std::cout << "[INFO] WebSocket handshake successful." << std::endl;

            beast::flat_buffer buffer;

            // Main read loop
            while (shouldRun)
            {
                try
                {
                    ws.read(buffer); // Blocking call to read data
                    std::string message = beast::buffers_to_string(buffer.data());
                    HandleMessage(message);
                    buffer.consume(buffer.size());
                }
                catch (const beast::system_error &se)
                {
                    // Handle WebSocket-specific errors
                    if (se.code() == websocket::error::closed)
                    {
                        std::cout << "[WARN] WebSocket closed by server." << std::endl;
                        break;
                    }
                    else
                    {
                        std::cerr << "[ERROR] WebSocket error: " << se.what() << std::endl;
                        break;
                    }
                }
            }

            std::cout << "[INFO] Reconnecting in 5 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
        catch (const std::exception &e)
        {
            std::cerr << "[ERROR] Connection failed: " << e.what() << std::endl;
            std::cout << "[INFO] Retrying connection in 5 seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    std::cout << "[INFO] WebSocket client stopping." << std::endl;
}

// Handle incoming messages
inline void WebSocketClient::HandleMessage(const std::string &message)
{
    std::cout << "[DEBUG] Received message: " << message << std::endl;

    if (message == "reload")
    {
        std::cout << "[INFO] Reload command received." << std::endl;
        try
        {
            // Simulate API call to fetch a file list
            std::cout << "[INFO] Fetching file list..." << std::endl;
            // g_state->apiClient.fetchFileList(); // Replace with your real API logic
            std::cout << "[INFO] File list reloaded successfully." << std::endl;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[ERROR] Error fetching file list: " << e.what() << std::endl;
        }
    }
}
