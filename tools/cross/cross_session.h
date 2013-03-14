#ifndef CROSS_SESSION_H_
#define CROSS_SESSION_H_
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/type_traits/aligned_storage.hpp>
#include <boost/type_traits/alignment_of.hpp>
namespace cross {

template<typename Derived>
class session {
 public:
    static const int kCommandSize = sizeof(command);

    ~session() {
        std::cout << "END" << std::endl;
    }

    session(boost::asio::io_service& io_service)
        : socket_(io_service) {
    }

    boost::asio::local::stream_protocol::socket& socket() {
        return socket_;
    }

    void start() {
        std::cout << "START" << std::endl;
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(&buffer_, kCommandSize),
              boost::bind(&session::handle_read, this, boost::asio::placeholders::error));
    }

 private:
    void handle_read(const boost::system::error_code& error) {
        if (error) {
            delete this;
            return;
        }
        const command command(*reinterpret_cast<cross::command*>(&buffer_));
        handle(command);

        // handle command
        boost::asio::async_write(
            socket_,
            boost::asio::buffer(&buffer_, kCommandSize),
            boost::bind(&session::handle_write, this, boost::asio::placeholders::error));
    }

    void handle_write(const boost::system::error_code& error) {
        if (error) {
            delete this;
            return;
        }

        boost::asio::async_read(
            socket_,
            boost::asio::buffer(&buffer_, kCommandSize),
            boost::bind(&session::handle_read, this, boost::asio::placeholders::error));
    }

    void handle(const command& command) {
        // std::cout
        //     << "type    : " << command.type << std::endl
        //     << "value   : " << command.value << std::endl
        //     << "offset  : " << command.offset << std::endl
        //     << "payload : " << command.payload << std::endl;
        static_cast<Derived*>(this)->handle(command);
    }

    boost::asio::local::stream_protocol::socket socket_;
    boost::aligned_storage<kCommandSize, boost::alignment_of<command>::value>::type buffer_;
};


}  // namespace cross
#endif  // CROSS_SESSION_H_
/* vim: set sw=4 ts=4 et tw=80 : */
