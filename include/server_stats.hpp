#pragma once

#include <atomic>
#include <cstdint>

struct ServerStats {
    std::atomic<std::uint64_t> total_requests{0};
    std::atomic<std::uint64_t> active_connections{0};
};

