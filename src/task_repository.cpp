#include "task_repository.hpp"

#include <algorithm>
#include <utility>

Task TaskRepository::create(std::string title) {
    std::scoped_lock lock(mutex_);
    Task task{next_id_++, std::move(title), false};
    tasks_.emplace(task.id, task);
    return task;
}

std::vector<Task> TaskRepository::list() const {
    std::scoped_lock lock(mutex_);
    std::vector<Task> result;
    result.reserve(tasks_.size());
    for (const auto& [id, task] : tasks_) {
        (void)id;
        result.push_back(task);
    }
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

