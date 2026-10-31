#pragma once
#include "Driver.hpp"
#include "VMManager.hpp"
#include "Policy.hpp"
#include "Metrics.hpp"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
class Autoscaler
{
    Config config_;
    VMManager &registry_;
    Driver &driver_;
    Policy policy_;
    Csv cpu_, events_, legacyCpu_;
    std::string owner_;
    std::vector<Resource> resources_;
    unsigned nextId_ = 2;
    std::mutex taskMutex_;
    std::condition_variable wake_;
    std::string task_;
    std::thread worker_;
    std::atomic_bool stopped_{false}, busy_{false};
    std::atomic_uint epoch_{0};
    unsigned seenEpoch_ = 0;
    void save();
    std::string ready(const std::string &name);
    void eraseResource(const std::string &name);
    void reconcile();
    void loop();
    void schedule(const std::string &action);

public:
    Autoscaler(const Config &, VMManager &, Driver &);
    ~Autoscaler();
    void initialize();
    void start();
    void stop();
    void tick(Driver &monitoringDriver);
    // Synchronous lifecycle entry points also used by fault-injected tests.
    void scaleOut();
    void scaleIn();
};
