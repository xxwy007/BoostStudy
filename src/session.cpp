#include "session.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/bind_handler.hpp>

#include <utility>

namespace beast = boost::beast;
namespace net = boost::asio;

HttpSession::HttpSession(net::ip::tcp::socket socket,
                         std::shared_ptr<Router> router,
                         std::shared_ptr<ServerStats> stats,
                         std::size_t body_limit)
    : stream_(std::move(socket)),
      router_(std::move(router)),
      stats_(std::move(stats)),
      body_limit_(body_limit) {
    stats_->active_connections.fetch_add(1, std::memory_order_relaxed);
}

HttpSession::~HttpSession() {
    if (counted_) {
        stats_->active_connections.fetch_sub(1, std::memory_order_relaxed);
    }
}

void HttpSession::run() {
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&HttpSession::read_request,
                                            shared_from_this()));
}

void HttpSession::read_request() {
    parser_.emplace();
    parser_->body_limit(body_limit_);
    stream_.expires_after(std::chrono::seconds(30));
    http::async_read(stream_, buffer_, *parser_,
                     beast::bind_front_handler(&HttpSession::on_read,
                                               shared_from_this()));
}

void HttpSession::on_read(beast::error_code error, std::size_t) {
    if (error == http::error::end_of_stream) {
        close();
        return;
    }
    if (error) {
        close();
        return;
    }

    auto request = parser_->release();
    write_response(router_->handle(request));
}

void HttpSession::write_response(HttpResponse response) {
    auto shared_response = std::make_shared<HttpResponse>(std::move(response));
    const bool close_after_write = shared_response->need_eof();
    stream_.expires_after(std::chrono::seconds(30));
    http::async_write(
        stream_, *shared_response,
        beast::bind_front_handler(&HttpSession::on_write,
                                  shared_from_this(),
                                  close_after_write,
                                  shared_response));
}

void HttpSession::on_write(bool close_after_write,
                           std::shared_ptr<HttpResponse>,
                           beast::error_code error,
                           std::size_t) {
    if (error) {
        close();
        return;
    }
    if (close_after_write) {
        close();
        return;
    }
    read_request();
}

void HttpSession::close() {
    beast::error_code ignored;
    stream_.socket().shutdown(net::ip::tcp::socket::shutdown_send, ignored);
    stream_.socket().close(ignored);
}

