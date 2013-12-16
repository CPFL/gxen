/*
 * A3 main entry point
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
#include "a3.h"
#include "context.h"
#include "device.h"
#include "cmdline.h"
namespace a3 {

class server {
 public:
    server(boost::asio::io_service& io_service, const char* endpoint, bool through)
        : io_service_(io_service)
        , acceptor_(io_service, boost::asio::local::stream_protocol::endpoint(endpoint))
        , through_(through) {
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
            new_session->start(through());
        } else {
            delete new_session;
        }
        start_accept();
    }

    bool through() const { return through_; }

    boost::asio::io_service& io_service_;
    boost::asio::local::stream_protocol::acceptor acceptor_;
    bool through_;
};

}  // namespace a3

int main(int argc, char** argv) {
    namespace c = a3;
    c::cmdline::Parser cmd("a3");


    cmd.Add("help", "help", 'h', "print this message");
    cmd.Add("version", "version", 'v', "print the version");
    cmd.Add("through", "through", 't', "through I/O");
    cmd.Add("lazy-shadowing", "lazy-shadowing", 0, "Enable lazy shadowing");
    cmd.Add("bar3-remapping", "bar3-remapping", 0, "Enable BAR3 remapping");
    cmd.set_footer("[program_file] [arguments]");

    if (!cmd.Parse(argc, argv)) {
        std::fprintf(stderr, "%s\n%s", cmd.error().c_str(), cmd.usage().c_str());
        return 1;
    }

    if (cmd.Exist("help")) {
        std::fputs(cmd.usage().c_str(), stdout);
        return 1;
    }

    if (cmd.Exist("version")) {
        std::printf("a3 %s (compiled %s %s)\n", A3_VERSION, __DATE__, __TIME__);
        return 1;
    }

    const std::vector<std::string>& rest = cmd.rest();

    c::bdf bdf = { { { 0, 0, 0 } } };

    if (rest.empty() || ((bdf.raw = strtol(rest.front().c_str(), nullptr, 16)) == 0)) {
        A3_FPRINTF(stderr, "Usage: a3 bdf\n");
        return 1;
    }

    A3_LOG("BDF: %02x:%02x.%01x\n", bdf.bus, bdf.dev, bdf.func);
    A3_LOG("through: %s\n", cmd.Exist("through") ? "enabled" : "disabled");

    // set flags
    a3::flags::lazy_shadowing = cmd.Exist("lazy-shadowing");
    a3::flags::bar3_remapping = cmd.Exist("bar3-remapping");

    c::device()->initialize(bdf);

    ::unlink(A3_ENDPOINT);
    try {
        boost::asio::io_service io_service;
        c::server s(io_service, A3_ENDPOINT, cmd.Exist("through"));
        io_service.run();
    } catch (std::exception& e) {
        A3_FPRINTF(stderr, "Exception: %s\n", e.what());
    }

    return 0;
}
/* vim: set sw=4 ts=4 et tw=80 : */
