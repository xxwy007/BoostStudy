#pragma once

#include "router.hpp"
#include "server_stats.hpp"

#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(boost::asio::ip::tcp::socket socket,
                std::shared_ptr<Router> router,
                std::shared_ptr<ServerStats> stats,
                std::size_t body_limit);
    ~HttpSession();

    void run();

private:
    void read_request();
    void on_read(boost::beast::error_code error, std::size_t bytes_transferred);
    void write_response(HttpResponse response);
    void on_write(bool close,
                  std::shared_ptr<HttpResponse> response,
                  boost::beast::error_code error,
                  std::size_t bytes_transferred);
    void close();

    boost::beast::tcp_stream stream_;
    boost::beast::flat_buffer buffer_;
    std::optional<http::request_parser<http::string_body>> parser_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<ServerStats> stats_;
    std::size_t body_limit_;
    bool counted_{true};
};

