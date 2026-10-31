#pragma once
#include "Config.hpp"
#include "Metrics.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
class LoadGenerator {
    Config config_;
    Csv requests_,events_;
    std::atomic_bool stopped_{false};
    std::atomic_ulong successes_{0}, failures_{0}, drops_{0};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<int> queue_;
    std::string mode_="stop";
    unsigned generation_=0;
    std::vector<std::thread> threads_;
    void schedule();
    void work();
    void membership();
public:
    explicit LoadGenerator(const Config&);
    ~LoadGenerator();
    void mode(const std::string&);
    void stop();
};
