#pragma once

#include "server_stats.hpp"
#include "task_repository.hpp"

#include <boost/beast/http.hpp>

#include <memory>

namespace http = boost::beast::http;

using HttpRequest = http::request<http::string_body>;
using HttpResponse = http::response<http::string_body>;

class Router {
public:
    Router(std::shared_ptr<TaskRepository> repository,
           std::shared_ptr<ServerStats> stats);

    HttpResponse handle(const HttpRequest& request) const;

private:
    std::shared_ptr<TaskRepository> repository_;
    std::shared_ptr<ServerStats> stats_;
};

