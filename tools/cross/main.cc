#include <cstdlib>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/array.hpp>
#include <unistd.h>
#include "cross.h"
namespace cross {

class session {
 public:
    static const int kMaxLength = 1024;

    session(boost::asio::io_service& io_service)
        : socket_(io_service) {
    }

    boost::asio::local::stream_protocol::socket& socket() {
        return socket_;
    }

    void start() {
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(buffer_, buffer_.size()),
              boost::bind(&session::handle_read, this, boost::asio::placeholders::error));
    }

 private:
    void handle_read(const boost::system::error_code& error) {
        if (error) {
            delete this;
            return;
        }
        command command;
        std::memcpy(reinterpret_cast<void*>(&command), buffer_.data(), buffer_.size());
        handle(command);

        // handle command
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(buffer_, buffer_.size()),
            boost::bind(&session::handle_write, this, boost::asio::placeholders::error));
    }

    void handle_write(const boost::system::error_code& error) {
        if (error) {
            delete this;
            return;
        }

        boost::asio::async_read(
            socket_,
            boost::asio::buffer(buffer_, buffer_.size()),
            boost::bind(&session::handle_read, this, boost::asio::placeholders::error));
    }

    void handle(const command& command) {
        std::cout
            << "type    : " << command.type() << std::endl
            << "value   : " << command.value() << std::endl
            << "offset  : " << command.offset() << std::endl
            << "payload : " << command.payload() << std::endl;
    }

    boost::asio::local::stream_protocol::socket socket_;
    boost::array<char, sizeof(command)> buffer_;
};

class server {
 public:
    server(boost::asio::io_service& io_service, const char* endpoint)
        : io_service_(io_service)
        , acceptor_(io_service, boost::asio::local::stream_protocol::endpoint(endpoint)) {
        start_accept();
    }

 private:
    void start_accept() {
        session* new_session = new session(io_service_);
        acceptor_.async_accept(
            new_session->socket(),
            boost::bind(&server::handle_accept, this, new_session, boost::asio::placeholders::error));
    }

    void handle_accept(session* new_session, const boost::system::error_code& error) {
        if (!error) {
            new_session->start();
        } else {
            delete new_session;
        }
        start_accept();
    }

    boost::asio::io_service& io_service_;
    boost::asio::local::stream_protocol::acceptor acceptor_;
};

}  // namespace cross

int main(int argc, char** argv) {
    ::unlink(CROSS_ENDPOINT);
    try {
        boost::asio::io_service io_service;
        cross::server s(io_service, CROSS_ENDPOINT);
        io_service.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
/* vim: set sw=4 ts=4 et tw=80 : */
