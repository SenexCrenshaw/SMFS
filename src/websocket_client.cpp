#include "websocket_client.hpp"
#include <iostream>
#include <thread>
#include <smfs_state.hpp>

WebSocketClient::WebSocketClient(std::string host, std::string port)
    : host_(std::move(host)), port_(std::move(port)),
      resolver_(ioc_), retryTimer_(ioc_) {}

WebSocketClient::~WebSocketClient()
{
    Logger::Log(LogLevel::INFO, "WebSocketClient destroyed.");
    Stop();
}

void WebSocketClient::Start()
{
    Logger::Log(LogLevel::DEBUG, "WebSocketClient::Start() called.");
    Connect();
    ioc_.run(); // Run the IO context loop
}

void WebSocketClient::Stop()
{
    Logger::Log(LogLevel::DEBUG, "WebSocketClient::Stop() called.");
    shouldRun_ = false;
    if (isConnected_)
    {
        Shutdown();
        isConnected_ = false;
    }
    ioc_.stop();
}

void WebSocketClient::SetMessageHandler(std::function<void(const std::string &)> handler)
{
    messageHandler_ = std::move(handler);
}

void WebSocketClient::Connect()
{
    if (!shouldRun_)
        return;

    Logger::Log(LogLevel::INFO, "Resolving host...");
    resolver_.async_resolve(host_, port_,
                            [this](beast::error_code ec, tcp::resolver::results_type results)
                            {
                                if (ec)
                                {
                                    Logger::Log(LogLevel::ERROR, "Resolve error: " + ec.message());
                                    RetryConnection();
                                    g_state->apiClient.fetchFileList();
                                    return;
                                }

                                ws_ = std::make_shared<websocket::stream<beast::tcp_stream>>(ioc_);
                                Logger::Log(LogLevel::INFO, "Connecting to host...");

                                net::async_connect(
                                    ws_->next_layer().socket(), // Access the actual socket
                                    results,                    // Pass the resolver results directly
                                    [this](beast::error_code ec, const tcp::endpoint &endpoint)
                                    {
                                        if (ec)
                                        {
                                            Logger::Log(LogLevel::ERROR, "Connect error: " + ec.message());
                                            RetryConnection();
                                            return;
                                        }

                                        Logger::Log(LogLevel::INFO, "Connected to host. Endpoint: " + endpoint.address().to_string());

                                        // Perform WebSocket handshake
                                        ws_->async_handshake(host_, "/ws",
                                                             [this](beast::error_code ec)
                                                             {
                                                                 if (ec)
                                                                 {
                                                                     Logger::Log(LogLevel::ERROR, "Handshake error: " + ec.message());
                                                                     RetryConnection();
                                                                     return;
                                                                 }

                                                                 isConnected_ = true;
                                                                 g_state->apiClient.fetchFileList();
                                                                 Logger::Log(LogLevel::INFO, "WebSocket connected successfully.");
                                                                 Read();
                                                             });
                                    });
                            });
}

void WebSocketClient::Read()
{
    if (!shouldRun_ || !ws_)
        return;

    ws_->async_read(buffer_,
                    [this](beast::error_code ec, std::size_t bytesTransferred)
                    {
                        if (ec)
                        {
                            if (ec == websocket::error::closed)
                            {
                                Logger::Log(LogLevel::WARN, "WebSocket closed by server.");
                            }
                            else
                            {
                                Logger::Log(LogLevel::ERROR, "Read error: " + ec.message());
                            }
                            isConnected_ = false;
                            RetryConnection();
                            return;
                        }

                        std::string message = beast::buffers_to_string(buffer_.data());
                        buffer_.consume(bytesTransferred);

                        Logger::Log(LogLevel::DEBUG, "Received message: " + message);
                        if (messageHandler_)
                        {
                            messageHandler_(message);
                        }

                        Read(); // Continue reading
                    });
}

void WebSocketClient::Shutdown()
{
    static std::atomic<bool> shutdownCalled{false};

    if (!ws_ || !isConnected_ || shutdownCalled.exchange(true))
    {
        Logger::Log(LogLevel::DEBUG, "Shutdown already in progress or not needed.");
        return;
    }

    Logger::Log(LogLevel::INFO, "Shutting down WebSocket...");
    ws_->async_close(websocket::close_code::normal,
                     [this](beast::error_code ec)
                     {
                         if (ec && ec != websocket::error::closed)
                         {
                             Logger::Log(LogLevel::ERROR, "Close error: " + ec.message());
                         }
                         else
                         {
                             Logger::Log(LogLevel::INFO, "WebSocket closed gracefully.");
                         }
                         isConnected_ = false;   // Ensure the flag is updated
                         shutdownCalled = false; // Reset for future use
                     });
}

void WebSocketClient::RetryConnection()
{
    if (!shouldRun_)
        return;

    static int retryDelay = 1;
    Logger::Log(LogLevel::INFO, "Retrying connection in " + std::to_string(retryDelay) + " seconds...");

    retryTimer_.expires_after(std::chrono::seconds(retryDelay));
    retryTimer_.async_wait([this](beast::error_code ec)
                           {
        if (!ec) {
            Connect();
        } });

    retryDelay = std::min(retryDelay * 2, 32); // Exponential backoff
}

void WebSocketClient::HandleMessage(std::string message)
{
    Logger::Log(LogLevel::DEBUG, "Processing message: " + message);

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
    else if (message == "shutdown")
    {
        Logger::Log(LogLevel::INFO, "Shutdown command received. Initiating shutdown...");
        exitRequested = true; // Set the global exit flag
    }
}
