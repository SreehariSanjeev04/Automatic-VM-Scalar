#pragma once

namespace Request {
    constexpr int num_threads = 8;
    constexpr int requests_per_threads_high = 50;
    constexpr int requests_per_threads_low = 50;
};

namespace Mode {
    constexpr int high = 1;
    constexpr int low = 0;
}