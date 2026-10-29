#include "task_repository.hpp"

#include <algorithm>
#include <utility>

Task TaskRepository::create(std::string title) {
    // ID 分配和数据插入必须处于同一个临界区，避免并发产生重复 ID。
    std::scoped_lock lock(mutex_);
    Task task{next_id_++, std::move(title), false};
    tasks_.emplace(task.id, task);
    return task;
}

std::vector<Task> TaskRepository::list() const {
    std::vector<Task> result;

    {
        // 互斥锁只保护共享容器的读取；离开作用域后立即释放。
        std::scoped_lock lock(mutex_);
        result.reserve(tasks_.size());
        for (const auto& [id, task] : tasks_) {
            (void)id;
            result.push_back(task);
        }
    }

    // unordered_map 无序，排序后接口输出和测试结果更加稳定。
    std::ranges::sort(result, {}, &Task::id);
    return result;
}

std::optional<Task> TaskRepository::find(std::uint64_t id) const {
    std::scoped_lock lock(mutex_);
    const auto iterator = tasks_.find(id);
    if (iterator == tasks_.end()) {
        return std::nullopt;
    }
    return iterator->second;
}

std::optional<Task> TaskRepository::update(
    std::uint64_t id,
    std::optional<std::string> title,
    std::optional<bool> completed) {
    std::scoped_lock lock(mutex_);
    const auto iterator = tasks_.find(id);
    if (iterator == tasks_.end()) {
        return std::nullopt;
    }
    if (title) {
        iterator->second.title = std::move(*title);
    }
    if (completed) {
        iterator->second.completed = *completed;
    }
    return iterator->second;
}

bool TaskRepository::erase(std::uint64_t id) {
    std::scoped_lock lock(mutex_);
    return tasks_.erase(id) != 0;
}

