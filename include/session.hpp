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

// 一个 HttpSession 对应一个 TCP 连接，可连续处理多个 Keep-Alive 请求。
// 异步回调通过 shared_from_this() 保证 Session 在操作完成前不会销毁。
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
    // 每次读取新请求时重建解析器，以便在读取前设置请求体大小限制。
    std::optional<http::request_parser<http::string_body>> parser_;
    std::shared_ptr<Router> router_;
    std::shared_ptr<ServerStats> stats_;
    std::size_t body_limit_;
    bool counted_{true};
};

