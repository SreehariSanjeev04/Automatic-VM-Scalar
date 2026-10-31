#pragma once
#include "json.hpp"
#include <condition_variable>
#include <map>
#include <vector>
#include <mutex>
#include <optional>
#include <string>
#include <chrono>
struct Backend
{
    std::string name, ip, state = "unready";
    bool owned = false;
    unsigned inFlight = 0;
};
class VMManager
{
    mutable std::mutex mutex_;
    std::condition_variable drained_;
    std::map<std::string, Backend> backends_;
    std::size_t next_ = 0;
    unsigned long version_ = 0;

public:
    void put(const Backend &backend);
    void erase(const std::string &name);
    void state(const std::string &name, const std::string &value);
    std::vector<Backend> snapshot() const;
    std::optional<Backend> acquire();
    void release(const std::string &name);
    bool drain(const std::string &name, std::chrono::milliseconds timeout);
    nlohmann::json membership() const;
};
class RequestLease
{
    VMManager &manager_;
    std::string name_;

public:
    RequestLease(VMManager &manager, const std::string &name) : manager_(manager), name_(name) {}
    ~RequestLease() { manager_.release(name_); }
};
