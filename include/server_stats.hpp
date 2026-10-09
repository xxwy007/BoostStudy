#pragma once

#include <atomic>
#include <cstdint>

struct ServerStats {
    // 统计值不参与业务同步，使用 relaxed 原子操作即可。
    std::atomic<std::uint64_t> total_requests{0};
    std::atomic<std::uint64_t> active_connections{0};
};

