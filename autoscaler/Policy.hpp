#pragma once
#include "Config.hpp"
#include <chrono>
#include <algorithm>
#include <map>
#include <string>
class Policy
{
    Config::Scaling config_;
    struct Counts
    {
        unsigned high = 0, low = 0;
    };
    std::map<std::string, Counts> counts_;
    std::chrono::steady_clock::time_point eligible_;

public:
    explicit Policy(const Config::Scaling &c) : config_(c), eligible_(std::chrono::steady_clock::now() + std::chrono::seconds(c.warmupSeconds)) {}
    void reset(bool cooldown = true)
    {
        counts_.clear();
        if (cooldown)
            eligible_ = std::chrono::steady_clock::now() + std::chrono::seconds(config_.cooldownSeconds);
    }
    void observe(const std::string &name, bool valid, double cpu)
    {
        auto &c = counts_[name];
        if (!valid || std::chrono::steady_clock::now() < eligible_)
        {
            c = {};
            return;
        }
        if (cpu > config_.scaleOutCpuThreshold)
        {
            c.high = std::min(c.high + 1, config_.requiredOverloadCount);
            c.low = 0;
        }
        else if (cpu < config_.scaleInCpuThreshold)
        {
            c.low = std::min(c.low + 1, config_.requiredLowLoadCount);
            c.high = 0;
        }
        else
            c = {};
    }
    bool overloaded(const std::string &name) const
    {
        auto it = counts_.find(name);
        return it != counts_.end() && it->second.high >= config_.requiredOverloadCount;
    }
    bool underloaded(const std::string &name) const
    {
        auto it = counts_.find(name);
        return it != counts_.end() && it->second.low >= config_.requiredLowLoadCount;
    }
};
