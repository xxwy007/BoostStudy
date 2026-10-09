#include "listener.hpp"

#include "session.hpp"

#include <boost/asio/strand.hpp>

#include <stdexcept>
#include <utility>

namespace net = boost::asio;
using tcp = net::ip::tcp;

Listener::Listener(net::io_context& io_context,
                   tcp::endpoint endpoint,
                   std::shared_ptr<Router> router,
                   std::shared_ptr<ServerStats> stats,
                   std::size_t body_limit)
    : io_context_(io_context),
      acceptor_(net::make_strand(io_context)),
      router_(std::move(router)),
      stats_(std::move(stats)),
      body_limit_(body_limit) {
    boost::system::error_code error;
    acceptor_.open(endpoint.protocol(), error);
    if (error) throw std::runtime_error("acceptor.open: " + error.message());

    acceptor_.set_option(net::socket_base::reuse_address(true), error);
    if (error) throw std::runtime_error("acceptor.set_option: " + error.message());

    acceptor_.bind(endpoint, error);
    if (error) throw std::runtime_error("acceptor.bind: " + error.message());

    acceptor_.listen(net::socket_base::max_listen_connections, error);
    if (error) throw std::runtime_error("acceptor.listen: " + error.message());
}

void Listener::run() {
    accept();
}

void Listener::accept() {
    // 每个新连接拥有独立 strand，同一个 Session 的回调不会并发执行。
    acceptor_.async_accept(
        net::make_strand(io_context_),
        [self = shared_from_this()](boost::system::error_code error,
                                    tcp::socket socket) {
            self->on_accept(error, std::move(socket));
        });
}

void Listener::on_accept(boost::system::error_code error, tcp::socket socket) {
    if (!error) {
        // 后续异步回调会通过 shared_from_this() 延长 Session 生命周期。
        std::make_shared<HttpSession>(std::move(socket), router_, stats_, body_limit_)->run();
    }
    if (acceptor_.is_open()) {
        accept();
    }
}

