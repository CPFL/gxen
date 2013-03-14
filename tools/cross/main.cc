#include <cstdlib>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <unistd.h>
#include "cross.h"
#include "cross_context.h"
namespace cross {

class server {
 public:
    server(boost::asio::io_service& io_service, const char* endpoint)
        : io_service_(io_service)
        , acceptor_(io_service, boost::asio::local::stream_protocol::endpoint(endpoint)) {
        start_accept();
    }

 private:
    void start_accept() {
        context* new_session = new context(io_service_);
        acceptor_.async_accept(
            new_session->socket(),
            boost::bind(&server::handle_accept, this, new_session, boost::asio::placeholders::error));
    }

    void handle_accept(context* new_session, const boost::system::error_code& error) {
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
