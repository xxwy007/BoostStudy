#pragma once

#include "router.hpp"
#include "server_stats.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstddef>
#include <memory>

class Listener : public std::enable_shared_from_this<Listener> {
public:
    Listener(boost::asio::io_context& io_context,
             boost::asio::ip::tcp::endpoint endpoint,
             std::shared_ptr<Router> router,
             std::shared_ptr<ServerStats> stats,
             std::size_t body_limit);

    void run();

private:
    void accept();
    void on_accept(boost::system::error_code error,
                   boost::asio::ip::tcp::socket socket);

    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<ServerStats> stats_;
    std::size_t body_limit_;
};

