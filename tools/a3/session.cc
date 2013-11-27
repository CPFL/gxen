/*
 * A3 session
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
#include <cstdio>
#include "session.h"
#include "context.h"
namespace a3 {

session::session(boost::asio::io_service& io_service)
    : socket_(io_service)
    , context_(nullptr)
    , thread_(nullptr)
    , req_queue_(nullptr)
    , res_queue_(nullptr)
{
}

session::~session() {
    if (thread_) {
        thread_->interrupt();
    }
}

void session::start(bool through) {
    context_.reset(new context(this, through));
    boost::asio::async_read(
	socket_,
	boost::asio::buffer(&buffer_, kCommandSize),
	  boost::bind(&session::handle_read, this, boost::asio::placeholders::error));
}

void session::main() {
    // this is main loop of message queue handling
    unsigned int priority;
    std::size_t size;
    A3_LOG("main loop start\n");
    for (;;) {
        command cmd;
        req_queue_->receive(&cmd, sizeof(command), size, priority);
        if (ctx()->handle(cmd)) {
            // res queue is needed
            res_queue_->send(buffer(), sizeof(command), 0);
        }
    }
}

void session::initialize(uint32_t id) {
    std::vector<char> name(200);

    // request queue
    {
        const int ret = std::snprintf(name.data(), name.size() - 1, "a3_shared_req_queue_%u", id);
        if (ret < 0) {
            std::perror(nullptr);
            std::exit(1);
        }
        name[ret] = '\0';

        // delete queue
        interprocess::message_queue::remove(name.data());

        // construct new queue
        req_queue_.reset(new interprocess::message_queue(interprocess::create_only, name.data(), 0x100000, sizeof(a3::command)));
    }

    // response queue
    {
        const int ret = std::snprintf(name.data(), name.size() - 1, "a3_shared_res_queue_%u", id);
        if (ret < 0) {
            std::perror(nullptr);
            std::exit(1);
        }
        name[ret] = '\0';

        // delete queue
        interprocess::message_queue::remove(name.data());

        // construct new queue
        res_queue_.reset(new interprocess::message_queue(interprocess::create_only, name.data(), 0x100000, sizeof(a3::command)));
    }

    thread_.reset(new boost::thread(&session::main, this));
}

void session::handle_read(const boost::system::error_code& error) {
    if (error) {
        delete this;
        return;
    }
    const command command(*buffer());
    ctx()->handle(command);

    // handle command
    boost::asio::async_write(
	socket_,
	boost::asio::buffer(&buffer_, kCommandSize),
	boost::bind(&session::handle_write, this, boost::asio::placeholders::error));
}

void session::handle_write(const boost::system::error_code& error) {
    if (error) {
        delete this;
        return;
    }

    boost::asio::async_read(
        socket_,
        boost::asio::buffer(&buffer_, kCommandSize),
        boost::bind(&session::handle_read, this, boost::asio::placeholders::error));
}

}  // namespace a3
/* vim: set sw=4 ts=4 et tw=80 : */
