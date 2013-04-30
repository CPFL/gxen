/*
 * Example client code
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

        sleep(60);

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
