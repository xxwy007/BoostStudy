#include "session.hpp"

#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/core/bind_handler.hpp>

#include <chrono>
#include <iostream>
#include <sstream>
#include <syncstream>
#include <thread>
#include <utility>

namespace beast = boost::beast;
namespace net = boost::asio;

namespace {

const auto process_started_at = std::chrono::steady_clock::now();

long long elapsed_milliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - process_started_at).count();
}

std::string error_details(beast::error_code error) {
    std::ostringstream details;
    details << "code=" << error.value()
            << " category=" << error.category().name()
            << " message=\"" << error.message() << '"';
    return details.str();
}

bool is_peer_disconnect(beast::error_code error) {
    return error == net::error::eof ||
           error == net::error::connection_reset ||
           error == net::error::broken_pipe;
}

} // namespace

HttpSession::HttpSession(net::ip::tcp::socket socket,
                         std::shared_ptr<Router> router,
                         std::shared_ptr<ServerStats> stats,
                         std::size_t body_limit)
    : stream_(std::move(socket)),
      router_(std::move(router)),
      stats_(std::move(stats)),
      session_id_(stats_->next_session_id.fetch_add(1, std::memory_order_relaxed)),
      body_limit_(body_limit) {
    stats_->active_connections.fetch_add(1, std::memory_order_relaxed);
    log_event("session_open");
}

HttpSession::~HttpSession() {
    log_event("session_destroy");
    if (counted_) {
        stats_->active_connections.fetch_sub(1, std::memory_order_relaxed);
    }
}

void HttpSession::log_event(std::string_view event,
                            std::string_view details) const {
    std::osyncstream output(std::cout);
    output << "[+" << elapsed_milliseconds() << "ms]"
           << " session=" << session_id_
           << " thread=" << std::this_thread::get_id()
           << " event=" << event;
    if (!details.empty()) {
        output << ' ' << details;
    }
    output << '\n';
}

void HttpSession::run() {
    // 将首次读取调度到这个连接所属的 executor/strand。
    log_event("session_run");
    net::dispatch(stream_.get_executor(),
                  beast::bind_front_handler(&HttpSession::read_request,
                                            shared_from_this()));
}

void HttpSession::read_request() {
    log_event("read_begin");
    parser_.emplace();
    // 必须在 async_read 前限制请求体，防止客户端发送超大消息。
    parser_->body_limit(body_limit_);
    stream_.expires_after(std::chrono::seconds(30));
    http::async_read(stream_, buffer_, *parser_,
                     beast::bind_front_handler(&HttpSession::on_read,
                                               shared_from_this()));
}

void HttpSession::on_read(beast::error_code error, std::size_t) {
    // end_of_stream 表示客户端正常结束了 HTTP 连接。
    if (error == http::error::end_of_stream) {
        log_event("peer_closed_normally");
        close();
        return;
    }
    if (error == beast::error::timeout) {
        log_event("read_timeout", error_details(error));
        close();
        return;
    }
    if (error == http::error::body_limit) {
        log_event("request_body_limit_exceeded", error_details(error));
        close();
        return;
    }
    if (error == net::error::operation_aborted) {
        log_event("read_cancelled", error_details(error));
        close();
        return;
    }
    if (is_peer_disconnect(error)) {
        log_event("peer_disconnected_during_read", error_details(error));
        close();
        return;
    }
    if (error) {
        log_event("read_error", error_details(error));
        close();
        return;
    }

    auto request = parser_->release();
    request_started_at_ = std::chrono::steady_clock::now();
    request_method_.assign(request.method_string().data(),
                           request.method_string().size());
    request_target_.assign(request.target().data(), request.target().size());
    std::ostringstream request_details;
    request_details << "method=" << request_method_
                    << " target=" << request_target_;
    log_event("read_complete", request_details.str());

    log_event("route_begin");
    auto response = router_->handle(request);
    log_event("route_complete",
              "status=" + std::to_string(response.result_int()));
    write_response(std::move(response));
}

void HttpSession::write_response(HttpResponse response) {
    // async_write 完成前响应对象必须存活，因此由完成回调共同持有。
    auto shared_response = std::make_shared<HttpResponse>(std::move(response));
    response_status_ = shared_response->result_int();
    log_event("write_begin",
              "status=" + std::to_string(response_status_));
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
                           std::size_t bytes_transferred) {
    if (error == net::error::operation_aborted) {
        log_event("write_cancelled", error_details(error));
        close();
        return;
    }
    if (is_peer_disconnect(error)) {
        log_event("peer_disconnected_during_write", error_details(error));
        close();
        return;
    }
    if (error) {
        log_event("write_error", error_details(error));
        close();
        return;
    }

    const auto duration_microseconds =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - request_started_at_).count();
    std::ostringstream access_details;
    access_details << "method=" << request_method_
                   << " target=" << request_target_
                   << " status=" << response_status_
                   << " duration_us=" << duration_microseconds
                   << " bytes_written=" << bytes_transferred;
    log_event("access", access_details.str());
    log_event("write_complete");

    request_method_.clear();
    request_target_.clear();
    response_status_ = 0;
    if (close_after_write) {
        close();
        return;
    }
    // Keep-Alive 响应发送完成后，继续读取同一连接上的下一次请求。
    read_request();
}

void HttpSession::close() {
    log_event("session_close");
    beast::error_code ignored;
    stream_.socket().shutdown(net::ip::tcp::socket::shutdown_send, ignored);
    stream_.socket().close(ignored);
}

