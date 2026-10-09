#pragma once

#include "task.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class TaskRepository {
public:
    Task create(std::string title);
    std::vector<Task> list() const;
    std::optional<Task> find(std::uint64_t id) const;
    std::optional<Task> update(
        std::uint64_t id,
        std::optional<std::string> title,
        std::optional<bool> completed);
    bool erase(std::uint64_t id);

private:
    // 多个 I/O 线程会共享仓库，容器和自增 ID 必须由同一把锁保护。
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, Task> tasks_;
    std::uint64_t next_id_{1};
};

