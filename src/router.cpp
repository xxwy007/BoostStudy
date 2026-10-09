#include "router.hpp"

#include <boost/json.hpp>

#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace json = boost::json;

namespace {

HttpResponse json_response(const HttpRequest& request,
                           http::status status,
                           json::value body) {
    HttpResponse response{status, request.version()};
    response.set(http::field::server, "beast-task-server");
    response.set(http::field::content_type, "application/json; charset=utf-8");
    // 保留请求的 Keep-Alive 语义，由 Session 决定继续读取还是关闭连接。
    response.keep_alive(request.keep_alive());
    response.body() = json::serialize(body);
    response.prepare_payload();
    return response;
}

HttpResponse error_response(const HttpRequest& request,
                            http::status status,
                            std::string_view code,
                            std::string_view message) {
    return json_response(request, status, json::object{
        {"error", std::string(code)},
        {"message", std::string(message)},
    });
}

std::string_view path_only(boost::beast::string_view target) {
    const std::string_view value{target.data(), target.size()};
    // 路由只匹配路径，查询字符串由具体接口按需解析。
    const auto query = value.find('?');
    return value.substr(0, query);
}

std::optional<std::uint64_t> task_id_from_path(std::string_view path) {
    constexpr std::string_view prefix = "/tasks/";
    if (!path.starts_with(prefix)) {
        return std::nullopt;
    }
    const auto text = path.substr(prefix.size());
    if (text.empty()) {
        return std::nullopt;
    }
    std::uint64_t id{};
    // from_chars 不抛异常；检查 end 可拒绝 /tasks/1abc 之类的路径。
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
    if (error != std::errc{} || end != text.data() + text.size() || id == 0) {
        return std::nullopt;
    }
    return id;
}

bool has_json_content_type(const HttpRequest& request) {
    const auto content_type = request[http::field::content_type];
    constexpr boost::beast::string_view expected = "application/json";
    return content_type.size() >= expected.size() &&
           content_type.substr(0, expected.size()) == expected;
}

std::optional<json::object> parse_object(const HttpRequest& request,
                                         boost::system::error_code& error) {
    auto value = json::parse(request.body(), error);
    if (error || !value.is_object()) {
        return std::nullopt;
    }
    return value.as_object();
}

} // namespace

Router::Router(std::shared_ptr<TaskRepository> repository,
               std::shared_ptr<ServerStats> stats)
    : repository_(std::move(repository)), stats_(std::move(stats)) {}

HttpResponse Router::handle(const HttpRequest& request) const {
    // 该计数只用于展示，不承担线程间同步职责。
    stats_->total_requests.fetch_add(1, std::memory_order_relaxed);
    const auto path = path_only(request.target());

    if (request.method() == http::verb::get && path == "/health") {
        return json_response(request, http::status::ok,
                             json::object{{"status", "ok"}});
    }

    if (request.method() == http::verb::get && path == "/stats") {
        return json_response(request, http::status::ok, json::object{
            {"total_requests", stats_->total_requests.load(std::memory_order_relaxed)},
            {"active_connections", stats_->active_connections.load(std::memory_order_relaxed)},
        });
    }

    if (path == "/tasks") {
        if (request.method() == http::verb::get) {
            json::array tasks;
            for (const auto& task : repository_->list()) {
                tasks.emplace_back(to_json(task));
            }
            return json_response(request, http::status::ok, std::move(tasks));
        }

        if (request.method() == http::verb::post) {
            if (!has_json_content_type(request)) {
                return error_response(request, http::status::unsupported_media_type,
                                      "unsupported_media_type",
                                      "Content-Type must be application/json");
            }
            boost::system::error_code error;
            const auto input = parse_object(request, error);
            if (!input) {
                return error_response(request, http::status::bad_request,
                                      "invalid_json", "Request body must be a JSON object");
            }
            const auto* title_value = input->if_contains("title");
            if (title_value == nullptr || !title_value->is_string()) {
                return error_response(request, http::status::bad_request,
                                      "invalid_title", "Field 'title' must be a string");
            }
            std::string title(title_value->as_string());
            if (title.empty() || title.size() > 200) {
                return error_response(request, http::status::bad_request,
                                      "invalid_title", "Title length must be between 1 and 200");
            }
            return json_response(request, http::status::created,
                                 to_json(repository_->create(std::move(title))));
        }

        return error_response(request, http::status::method_not_allowed,
                              "method_not_allowed", "Method is not allowed for /tasks");
    }

    if (path.starts_with("/tasks/")) {
        const auto id = task_id_from_path(path);
        if (!id) {
            return error_response(request, http::status::bad_request,
                                  "invalid_task_id", "Task id must be a positive integer");
        }

        if (request.method() == http::verb::get) {
            const auto task = repository_->find(*id);
            if (!task) {
                return error_response(request, http::status::not_found,
                                      "task_not_found", "Task does not exist");
            }
            return json_response(request, http::status::ok, to_json(*task));
        }

        if (request.method() == http::verb::put) {
            if (!has_json_content_type(request)) {
                return error_response(request, http::status::unsupported_media_type,
                                      "unsupported_media_type",
                                      "Content-Type must be application/json");
            }
            boost::system::error_code error;
            const auto input = parse_object(request, error);
            if (!input) {
                return error_response(request, http::status::bad_request,
                                      "invalid_json", "Request body must be a JSON object");
            }

            std::optional<std::string> title;
            std::optional<bool> completed;
            if (const auto* value = input->if_contains("title")) {
                if (!value->is_string()) {
                    return error_response(request, http::status::bad_request,
                                          "invalid_title", "Field 'title' must be a string");
                }
                title = std::string(value->as_string());
                if (title->empty() || title->size() > 200) {
                    return error_response(request, http::status::bad_request,
                                          "invalid_title", "Title length must be between 1 and 200");
                }
            }
            if (const auto* value = input->if_contains("completed")) {
                if (!value->is_bool()) {
                    return error_response(request, http::status::bad_request,
                                          "invalid_completed", "Field 'completed' must be boolean");
                }
                completed = value->as_bool();
            }
            if (!title && !completed) {
                return error_response(request, http::status::bad_request,
                                      "empty_update", "Provide 'title' or 'completed'");
            }

            const auto task = repository_->update(*id, std::move(title), completed);
            if (!task) {
                return error_response(request, http::status::not_found,
                                      "task_not_found", "Task does not exist");
            }
            return json_response(request, http::status::ok, to_json(*task));
        }

        if (request.method() == http::verb::delete_) {
            if (!repository_->erase(*id)) {
                return error_response(request, http::status::not_found,
                                      "task_not_found", "Task does not exist");
            }
            HttpResponse response{http::status::no_content, request.version()};
            response.set(http::field::server, "beast-task-server");
            response.keep_alive(request.keep_alive());
            return response;
        }

        return error_response(request, http::status::method_not_allowed,
                              "method_not_allowed", "Method is not allowed for this task");
    }

    return error_response(request, http::status::not_found,
                          "route_not_found", "Route does not exist");
}

