#pragma once

namespace Request {
    constexpr int num_threads = 1;
    constexpr int requests_per_threads_high = 1;
    constexpr int requests_per_threads_low = 1;
};

namespace Mode {
    constexpr int high = 1;
    constexpr int low = 0;
}