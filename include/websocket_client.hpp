// File: websocket_client.hpp
#ifndef WEBSOCKET_CLIENT_HPP
#define WEBSOCKET_CLIENT_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <string>
#include <atomic>
#include <functional>
#include "logger.hpp"

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

class WebSocketClient
{
public:
    /// Constructs a WebSocket client with server details and an optional message handler.
    WebSocketClient(std::string host, std::string port);

    /// Destructor ensures proper cleanup.
    ~WebSocketClient();

    /// Starts the WebSocket client asynchronously.
    void Start();

    /// Stops the WebSocket client gracefully.
    void Stop();

    /// Sets a custom message handler to process inbound messages.
    void SetMessageHandler(std::function<void(const std::string &)> handler);

private:
    /// Resolves the host and connects to the WebSocket server.
    void Connect();

    /// Initiates reading messages from the WebSocket.
    void Read();

    /// Gracefully shuts down the WebSocket.
    void Shutdown();

    /// Retries connection with exponential backoff.
    void RetryConnection();

    /// Processes inbound WebSocket messages.
    void HandleMessage(std::string message);

    std::string host_;
    std::string port_;

    net::io_context ioc_;
    tcp::resolver resolver_;
    std::shared_ptr<websocket::stream<beast::tcp_stream>> ws_;
    net::steady_timer retryTimer_;
    beast::flat_buffer buffer_;

    std::atomic<bool> shouldRun_{true};
    std::atomic<bool> isConnected_{false};

    std::function<void(const std::string &)> messageHandler_;
};

#endif // WEBSOCKET_CLIENT_HPP
