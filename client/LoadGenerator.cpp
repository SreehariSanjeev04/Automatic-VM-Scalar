#include "LoadGenerator.hpp"
#include "httplib.h"
#include "json.hpp"
#include <iostream>
using json = nlohmann::json;
LoadGenerator::LoadGenerator(const Config &c) : config_(c),
                                                requests_(c.metricsDirectory, "client.csv", "timestamp,status,latency_ms,backend,error"),
                                                events_(c.metricsDirectory, "client-events.csv", "timestamp,event,detail")
{
    for (unsigned i = 0; i < c.traffic.highThreads; ++i)
        threads_.emplace_back([this]
                              { work(); });
    threads_.emplace_back([this]
                          { schedule(); });
    threads_.emplace_back([this]
                          { membership(); });
}
LoadGenerator::~LoadGenerator() { stop(); }
void LoadGenerator::mode(const std::string &mode)
{
    if (mode != "increase" && mode != "decrease" && mode != "stop")
        throw std::invalid_argument("Unknown traffic mode");
    {
        std::lock_guard lock(mutex_);
        mode_ = mode;
        ++generation_;
        drops_ += queue_.size();
        queue_.clear();
    }
    events_.row({"mode", mode});
    wake_.notify_all();
}
void LoadGenerator::stop()
{
    if (stopped_.exchange(true))
        return;
    {
        std::lock_guard lock(mutex_);
        drops_ += queue_.size();
        queue_.clear();
    }
    wake_.notify_all();
    for (auto &t : threads_)
        if (t.joinable())
            t.join();
    events_.row({"totals", "success=" + std::to_string(successes_) + " failed=" + std::to_string(failures_) + " dropped=" + std::to_string(drops_)});
}
void LoadGenerator::schedule()
{
    std::unique_lock lock(mutex_);
    auto next = std::chrono::steady_clock::now();
    unsigned generation = generation_;
    while (!stopped_)
    {
        if (mode_ == "stop")
        {
            wake_.wait(lock, [&]
                       { return stopped_ || mode_ != "stop"; });
            next = std::chrono::steady_clock::now();
        }
        if (stopped_)
            break;
        if (generation != generation_)
        {
            generation = generation_;
            next = std::chrono::steady_clock::now();
        }
        if (wake_.wait_until(lock, next, [&]
                             { return stopped_ || generation != generation_; }))
            continue;
        bool high = mode_ == "increase";
        if (queue_.size() < config_.traffic.queueCapacity)
            queue_.push_back(high ? config_.traffic.highValue : config_.traffic.lowValue);
        else
        {
            ++drops_;
            events_.row({"drop", "queue_full"});
        }
        wake_.notify_all();
        auto spacing = std::chrono::duration<double>(1.0 / (high ? config_.traffic.highRate : config_.traffic.lowRate));
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(spacing);
        // Skip missed slots rather than emitting a burst after a scheduling stall.
        if (next < std::chrono::steady_clock::now())
            next = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(spacing);
    }
}
void LoadGenerator::work()
{
    httplib::Client client(config_.network.loadBalancerHost, config_.network.loadBalancerPort);
    client.set_connection_timeout(config_.network.connectionTimeoutSeconds, 0);
    client.set_read_timeout(config_.network.clientTimeoutSeconds, 0);
    client.set_write_timeout(config_.network.clientTimeoutSeconds, 0);
    while (!stopped_)
    {
        int value;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [&]
                       { return stopped_ || !queue_.empty(); });
            if (stopped_)
                break;
            value = queue_.front();
            queue_.pop_front();
        }
        auto start = std::chrono::steady_clock::now();
        auto response = client.Post(config_.network.loadBalancerPath, json({{"value", value}}).dump(), "application/json");
        int status = response ? response->status : 0;
        std::string backend, error;
        if (response && status >= 200 && status < 300)
        {
            ++successes_;
            try
            {
                backend = json::parse(response->body).value("backend", "");
            }
            catch (const json::exception &)
            {
            }
        }
        else
        {
            ++failures_;
            error = response ? response->body : httplib::to_string(response.error());
        }
        auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        requests_.row({std::to_string(status), std::to_string(ms), backend, error});
    }
}
void LoadGenerator::membership()
{
    httplib::Client client(config_.network.loadBalancerHost, config_.network.loadBalancerPort);
    client.set_connection_timeout(config_.network.connectionTimeoutSeconds, 0);
    client.set_read_timeout(5, 0);
    long version = -1;
    while (!stopped_)
    {
        if (auto response = client.Get("/replicas"); response && response->status == 200)
        {
            try
            {
                auto body = json::parse(response->body);
                auto current = body.at("version").get<long>();
                if (current != version)
                {
                    version = current;
                    events_.row({"membership", body.dump()});
                    std::cout << "Replicas: " << body.at("replicas").dump() << std::endl;
                }
            }
            catch (const json::exception &)
            {
            }
        }
        std::cout << "Requests: success=" << successes_ << " failed=" << failures_ << " dropped=" << drops_ << std::endl;
        std::unique_lock lock(mutex_);
        wake_.wait_for(lock, std::chrono::seconds(2), [&]
                       { return stopped_.load(); });
    }
}
