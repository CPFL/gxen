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
#include "a3_session.h"
#include "a3_context.h"
namespace a3 {

session::session(boost::asio::io_service& io_service)
    : socket_(io_service)
    , context_(NULL) {
}

session::~session() { }

void session::start(bool through) {
    A3_LOG("START\n");
    context_.reset(new context(this, through));
    ctx()->accept();
    boost::asio::async_read(
	socket_,
	boost::asio::buffer(&buffer_, kCommandSize),
	  boost::bind(&session::handle_read, this, boost::asio::placeholders::error));
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
