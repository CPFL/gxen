/*
 * NVIDIA NVC0 Context
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

#include <unistd.h>
#include <signal.h>
#include "nvc0.h"
#include "nvc0_context.h"
#include "cross.h"
namespace nvc0 {

context::context(nvc0_state_t* state, uint64_t memory_size)
    : state_(state)
    , bar1_table_(0x10001)
    , bar3_table_(0x10003)
    , barrier_()
    , tlb_()
    , poll_()
    , remapping_(memory_size)
    , pramin_()
    , io_service_()
    , socket_(io_service_)
    , socket_mutex_() {

    // initialize connection
    socket_.connect(boost::asio::local::stream_protocol::endpoint(CROSS_ENDPOINT));

    // send guest id to cross
    cross::command cmd = {
        cross::command::TYPE_INIT,
        nvc0_domid(),
        0,
        0
    };
    send(cmd);
}

cross::command context::send(const cross::command& cmd) {
    boost::mutex::scoped_lock lock(socket_mutex_);
    cross::command result = {
        0xdeadface,
        0xdeadface,
        0xdeadface,
        0xdeadface
    };
    while (true) {
        boost::system::error_code error;
        boost::asio::write(
            socket_,
            boost::asio::buffer(reinterpret_cast<const char*>(&cmd), sizeof(cross::command)),
            boost::asio::transfer_all(),
            error);
        if (error != boost::asio::error::make_error_code(boost::asio::error::interrupted)) {
            break;
        }
        // retry
    }
    while (true) {
        boost::system::error_code error;
        boost::asio::read(
            socket_,
            boost::asio::buffer(reinterpret_cast<char*>(&result), sizeof(cross::command)),
            boost::asio::transfer_all(),
            error);
        if (error != boost::asio::error::make_error_code(boost::asio::error::interrupted)) {
            break;
        }
    }
    return result;
}

context* context::extract(nvc0_state_t* state) {
    return static_cast<context*>(state->priv);
}

}  // namespace nvc0

extern "C" void nvc0_context_init(nvc0_state_t* state) {
    // currenty, 3GB
    state->priv = static_cast<void*>(new nvc0::context(state, 2 * 1024 * 1024 * 1024));
}
/* vim: set sw=4 ts=4 et tw=80 : */
