// File: websocket_client.cpp
#include "websocket_client.hpp"
#include "logger.hpp"
#include "smfs_state.hpp" // For global state and exitRequested

#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <atomic>

WebSocketClient::WebSocketClient(const std::string &host, const std::string &port, const std::string &apiKey)
    : host_(host), port_(port), apiKey_(apiKey)
{
}

WebSocketClient::~WebSocketClient()
{
    Stop();
    if (wsThread.joinable())
    {
        wsThread.join();
    }
}

void WebSocketClient::Start()
{
    Logger::Log(LogLevel::DEBUG, "WebSocketClient::Start() called.");

    wsThread = std::thread([this]()
                           {
                               Logger::Log(LogLevel::DEBUG, "WebSocketClient thread started.");
                               ConnectAndListen(); });
}

void WebSocketClient::Stop()
{
    Logger::Log(LogLevel::DEBUG, "WebSocketClient::Stop() called.");
    if (!shouldRun.exchange(false))
    {
        Logger::Log(LogLevel::WARN, "WebSocket client is already stopped.");
        return;
    }

    auto start = std::chrono::steady_clock::now();

    // Wait for the thread to exit
    while (wsThread.joinable())
    {
        if (std::chrono::steady_clock::now() - start > std::chrono::seconds(5))
        {
            Logger::Log(LogLevel::WARN, "WebSocket thread did not exit in time. Forcing shutdown...");
            pthread_cancel(wsThread.native_handle());
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (wsThread.joinable())
    {
        try
        {
            wsThread.join();
        }
        catch (const std::system_error &e)
        {
            Logger::Log(LogLevel::ERROR, "Error joining WebSocket thread: " + std::string(e.what()));
        }
    }

    Logger::Log(LogLevel::INFO, "WebSocket client stopped.");
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

            // Attempt to connect
            Logger::Log(LogLevel::INFO, "Attempting WebSocket connection...");
            ws.next_layer().connect(*results.begin());
            ws.handshake(host_, "/ws");
            Logger::Log(LogLevel::INFO, "WebSocket connection established.");

            // Fetch the file list after reconnecting
            if (g_state != nullptr)
            {
                Logger::Log(LogLevel::INFO, "Fetching file list after reconnecting...");
                g_state->apiClient.fetchFileList();
                Logger::Log(LogLevel::INFO, "File list fetched successfully after reconnecting.");
            }

            beast::flat_buffer buffer;
            while (shouldRun)
            {
                try
                {
                    // Read messages
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

            // Cleanly close the WebSocket
            if (shouldRun)
            {
                Logger::Log(LogLevel::INFO, "Closing WebSocket connection...");
                ws.close(websocket::close_code::normal);
            }
        }
        catch (const std::exception &e)
        {
            Logger::Log(LogLevel::ERROR, "WebSocket connection failed: " + std::string(e.what()));
        }

        if (!shouldRun)
            break;

        // Reconnect with exponential backoff
        Logger::Log(LogLevel::INFO, "Retrying connection in " + std::to_string(retryDelay) + " seconds.");
        std::this_thread::sleep_for(std::chrono::seconds(retryDelay));
        retryDelay = std::min(retryDelay * 2, 32); // Exponential backoff up to 32 seconds
    }

    Logger::Log(LogLevel::INFO, "WebSocketClient::ConnectAndListen exiting.");
}

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
    else if (message == "shutdown")
    {
        Logger::Log(LogLevel::INFO, "Shutdown command received. Initiating shutdown...");
        exitRequested = true; // Set the global exit flag
    }
    // Add additional command handling here
}
