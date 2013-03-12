#include <cstdlib>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <unistd.h>
#include "cross.h"

class session {
public:
    session(boost::asio::io_service& io_service)
        : socket_(io_service) {
    }

    boost::asio::local::stream_protocol::socket& socket() {
        return socket_;
    }

    void start() {
        socket_.async_read_some(
          boost::asio::buffer(data_, max_length),
            boost::bind(&session::handle_read, this,
              boost::asio::placeholders::error,
              boost::asio::placeholders::bytes_transferred));
    }

 private:
    void handle_read(const boost::system::error_code& error, size_t bytes_transferred) {
      if (!error) {
          boost::asio::async_write(socket_,
              boost::asio::buffer(data_, bytes_transferred),
              boost::bind(&session::handle_write, this,
                boost::asio::placeholders::error));
      } else {
          delete this;
      }
    }

    void handle_write(const boost::system::error_code& error) {
      if (!error) {
          socket_.async_read_some(
              boost::asio::buffer(data_, max_length),
              boost::bind(&session::handle_read, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
      } else {
          delete this;
      }
    }

    boost::asio::local::stream_protocol::socket socket_;
    enum { max_length = 1024 };
    char data_[max_length];
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

int main(int argc, char** argv) {
    ::unlink(CROSS_ENDPOINT);

    try {
        boost::asio::io_service io_service;
        server s(io_service, CROSS_ENDPOINT);
        io_service.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
/* vim: set sw=4 ts=4 et tw=80 : */
