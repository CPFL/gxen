// This is example file
#include <cstdlib>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <unistd.h>
#include "../cross.h"

int main(int argc, char** argv) {
    try {
        boost::asio::io_service io_service;
        boost::asio::local::stream_protocol::endpoint ep(CROSS_ENDPOINT);
        boost::asio::local::stream_protocol::socket socket(io_service);
        socket.connect(ep);

        cross::command command = {
            0,
            1,
            2,
            3
        };

        boost::asio::write(
            socket,
            boost::asio::buffer(reinterpret_cast<char*>(&command), sizeof(cross::command)));

        boost::asio::read(
            socket,
            boost::asio::buffer(reinterpret_cast<char*>(&command), sizeof(cross::command)));

        boost::asio::write(
            socket,
            boost::asio::buffer(reinterpret_cast<char*>(&command), sizeof(cross::command)));
        std::cout << "send correct!" << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
/* vim: set sw=4 ts=4 et tw=80 : */
