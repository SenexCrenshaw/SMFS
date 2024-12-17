#pragma once

#include <uwebsockets/App.h>
#include "logger.hpp"
#include "api_client.hpp"
#include "smfs_state.hpp"

struct WebSocketData
{
};

class WebSocketClient
{
public:
    WebSocketClient(const std::string &host, const std::string &port, const std::string &apiKey)
        : host(host), port(port), apiKey(apiKey)
    {
        webSocketUrl = "ws://" + host + ":" + port + "/ws";
    }

    void Start();

private:
    static void OnMessage(uWS::WebSocket<false, true, WebSocketData> *ws, std::string_view message, uWS::OpCode opCode);
    static void OnClose(uWS::WebSocket<false, true, WebSocketData> *ws, int code, std::string_view message);

    std::string webSocketUrl;
    std::string host;
    std::string port;
    std::string apiKey;
};

// Connect without capturing [this]

inline void WebSocketClient::Start()
{
    std::thread wsThread([this]()
                         {
                             Logger::Log(LogLevel::INFO, "Connecting to WebSocket server: " + webSocketUrl);

                             uWS::App()
                                 .connect(
                                     webSocketUrl,
                                     [](uWS::HttpResponse<false> *res, uWS::HttpRequest *req)
                                     {
                                         Logger::Log(LogLevel::INFO, "WebSocket handshake started.");
                                     })
                                 .ws<WebSocketData>("/*", {
                                                       .open = [](auto *ws)
                                                       {
                                                           Logger::Log(LogLevel::INFO, "WebSocket connection established.");
                                                       },
                                                       .message = WebSocketClient::OnMessage,
                                                       .close = WebSocketClient::OnClose,
                                                   })
                                 .run();

                             Logger::Log(LogLevel::INFO, "WebSocket client event loop running..."); });

    wsThread.detach();
}

inline void WebSocketClient::OnClose(uWS::WebSocket<false, true, WebSocketData> *ws, int code, std::string_view message)
{
    Logger::Log(LogLevel::WARN, "WebSocket connection closed.");
}

// Handle incoming messages
inline void WebSocketClient::OnMessage(uWS::WebSocket<false, true, WebSocketData> *ws, std::string_view message, uWS::OpCode opCode)
{
    std::string msg(message);
    Logger::Log(LogLevel::DEBUG, "Message received: " + msg);

    if (msg == "reload")
    {
        Logger::Log(LogLevel::INFO, "Reload command received.");
        g_state->apiClient.fetchFileList();
        Logger::Log(LogLevel::INFO, "File list reloaded successfully.");
    }
}