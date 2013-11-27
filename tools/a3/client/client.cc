/*
 * A3 cli client
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
#include "../a3.h"
#include "../a3_cmdline.h"

int main(int argc, char** argv) {
    namespace c = a3;
    c::cmdline::Parser cmd("a3-client");

    cmd.Add("help", "help", 'h', "print this message");
    cmd.Add("version", "version", 'v', "print the version");
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

    a3::command command = {
        a3::command::TYPE_UTILITY,
        0
    };

    if (rest.empty()) {
        command.value = a3::command::UTILITY_CLEAR_SHADOWING_UTILIZATION;
    } else if (rest.front() == "register" && rest.size() >= 2) {
        command.value = a3::command::UTILITY_REGISTER_READ;
        command.offset = strtol(rest[1].c_str(), NULL, 16);
    } else {
        return 1;
    }

    try {
        boost::asio::io_service io_service;
        boost::asio::local::stream_protocol::endpoint ep(A3_ENDPOINT);
        boost::asio::local::stream_protocol::socket socket(io_service);
        socket.connect(ep);

        boost::asio::write(
            socket,
            boost::asio::buffer(reinterpret_cast<char*>(&command), sizeof(a3::command)));
        boost::asio::read(
            socket,
            boost::asio::buffer(reinterpret_cast<char*>(&command), sizeof(a3::command)));
        std::cout << command.value << std::endl;
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
    }

    return 0;
}
/* vim: set sw=4 ts=4 et tw=80 : */
