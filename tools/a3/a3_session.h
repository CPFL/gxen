#ifndef A3_SESSION_H_
#define A3_SESSION_H_
#include <boost/noncopyable.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/type_traits/aligned_storage.hpp>
#include <boost/type_traits/alignment_of.hpp>
#include <boost/scoped_ptr.hpp>
#include "a3.h"
namespace a3 {

class context;

class session : private boost::noncopyable {
 public:
    static const int kCommandSize = sizeof(command);

    virtual ~session();
    session(boost::asio::io_service& io_service);
    void start(bool through);
    boost::asio::local::stream_protocol::socket& socket() { return socket_; }
    command* buffer() { return reinterpret_cast<a3::command*>(&buffer_); }
    context* ctx() const { return context_.get(); }

 private:
    void handle_read(const boost::system::error_code& error);
    void handle_write(const boost::system::error_code& error);

    boost::asio::local::stream_protocol::socket socket_;
    boost::aligned_storage<kCommandSize, boost::alignment_of<command>::value>::type buffer_;
    boost::scoped_ptr<context> context_;
};


}  // namespace a3
#endif  // A3_SESSION_H_
/* vim: set sw=4 ts=4 et tw=80 : */
