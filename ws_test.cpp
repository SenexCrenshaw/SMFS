#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <iostream>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = net::ip::tcp;

int main()
{
    try
    {
        const std::string host = "10.6.10.50";
        const std::string port = "7095";
        const std::string target = "/ws";

        net::io_context ioc;
        tcp::resolver resolver{ioc};
        auto const results = resolver.resolve(host, port);

        websocket::stream<beast::tcp_stream> ws{ioc};
        ws.next_layer().connect(*results.begin());
        std::cout << "[INFO] Connected to " << host << ":" << port << std::endl;

        ws.handshake(host, target);
        std::cout << "[INFO] WebSocket handshake successful." << std::endl;

        // Send a message
        ws.write(net::buffer(std::string("Test message")));
        std::cout << "[INFO] Test message sent." << std::endl;

        // Receive a message
        beast::flat_buffer buffer;
        ws.read(buffer);
        std::cout << "[INFO] Received: " << beast::buffers_to_string(buffer.data()) << std::endl;

        ws.close(websocket::close_code::normal);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[ERROR] " << e.what() << std::endl;
    }

    return 0;
}
