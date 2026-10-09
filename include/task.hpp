#pragma once

#include <boost/json/object.hpp>

#include <cstdint>
#include <string>

struct Task {
    std::uint64_t id{};
    std::string title;
    bool completed{false};
};

inline boost::json::object to_json(const Task& task) {
    return {
        {"id", task.id},
        {"title", task.title},
        {"completed", task.completed},
    };
}

