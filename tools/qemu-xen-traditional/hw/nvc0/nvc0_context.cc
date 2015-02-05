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
#include "nvc0_mmio.h"
#include "nvc0_para_virt.h"
#include "a3.h"

namespace nvc0 {

context::context(nvc0_state_t* state, uint64_t memory_size)
    : state_(state)
    , pramin_()
    , mmio_counter_()
    , io_service_()
    , socket_(io_service_)
    , socket_mutex_()
    , req_queue_()
    , res_queue_()
    , mmio_bar0_()
    , mmio_bar1_()
    , mmio_bar3_()
{

    // initialize connection
    socket_.connect(boost::asio::local::stream_protocol::endpoint(A3_ENDPOINT));

    // send guest id to a3
    a3::command cmd = {
        a3::command::TYPE_INIT,
        nvc0_domid(),
        nvc0_guest_id
    };
    const a3::command res = send(cmd);
    id_ = res.value;

    // initialize req/res queue
    std::vector<char> name(200);
    {

        const int ret = std::snprintf(name.data(), name.size() - 1, "a3_shared_req_queue_%u", id());
        if (ret < 0) {
            std::perror(NULL);
            std::exit(1);
        }
        name[ret] = '\0';

        req_queue_.reset(new a3::interprocess::message_queue(a3::interprocess::open_only, name.data()));
    }

    {
        const int ret = std::snprintf(name.data(), name.size() - 1, "a3_shared_res_queue_%u", id());
        if (ret < 0) {
            std::perror(NULL);
            std::exit(1);
        }
        name[ret] = '\0';

        // construct new queue
        res_queue_.reset(new a3::interprocess::message_queue(a3::interprocess::open_only, name.data()));
    }
}

a3::command context::send(const a3::command& cmd) {
    boost::mutex::scoped_lock lock(socket_mutex_);
    a3::command result = { };
    while (true) {
        boost::system::error_code error;
        boost::asio::write(
            socket_,
            boost::asio::buffer(reinterpret_cast<const char*>(&cmd), sizeof(a3::command)),
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
            boost::asio::buffer(reinterpret_cast<char*>(&result), sizeof(a3::command)),
            boost::asio::transfer_all(),
            error);
        if (error != boost::asio::error::make_error_code(boost::asio::error::interrupted)) {
            break;
        }
    }
    return result;
}

a3::command context::message(const a3::command& cmd, bool read) {
    boost::mutex::scoped_lock lock(socket_mutex_);
    req_queue_->send(&cmd, sizeof(a3::command), 0);
    if (read) {
        a3::command result = { };
        unsigned int priority;
        std::size_t size;
        res_queue_->receive(&result, sizeof(a3::command), size, priority);
        instrument(result.is_tlb(), result.is_shadowing());
        return result;
    }
    return a3::command();
}

context* context::extract(nvc0_state_t* state) {
    return static_cast<context*>(state->priv);
}

void context::notify_bar3_change() {
    const uint64_t address = state_->bar[3].addr;
    const a3::command cmd = {
        a3::command::TYPE_BAR3,
        static_cast<uint32_t>(address >> 32),
        static_cast<uint32_t>(address)
    };
    send(cmd);
}

void context::instrument(bool tlb, bool shadowing) {
    mmio_counter_ += 1;
    // std::printf("LOG %010" PRIu64 ":TLB:%d SHW:%d\n",  mmio_counter_, tlb ? 1 : 0, shadowing ? 1 : 0);
    // std::fflush(stdout);
}

void context::instrument_bar(int bar) {
    if (bar == 0) {
        mmio_bar0_ += 1;
    } else if (bar == 1) {
        mmio_bar1_ += 1;
    } else if (bar == 3) {
        mmio_bar3_ += 1;
    }
    std::printf("BAR BAR0:(%llu),BAR1:(%llu),BAR3:(%llu)\n",  mmio_bar0_, mmio_bar1_, mmio_bar3_);
    std::fflush(stdout);
}

}  // namespace nvc0

extern "C" void nvc0_context_init(nvc0_state_t* state) {
    // currenty, 1GB
    state->priv = static_cast<void*>(new nvc0::context(state, A3_MEMORY_SIZE));
}
/* vim: set sw=4 ts=4 et tw=80 : */
