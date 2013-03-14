/*
 * Cross main entry point
 *
 * Copyright (c) 2012-2013 Yusuke Suzuki
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
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
