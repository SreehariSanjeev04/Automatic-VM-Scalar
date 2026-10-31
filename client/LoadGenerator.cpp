#include "LoadGenerator.hpp"
#include "httplib.h"
#include "json.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using json = nlohmann::json;

namespace
{
    std::atomic_bool stopRequested{false};
    std::atomic_bool running{false};
    Config config;
}

void LoadGenerator::configure(const Config &value)
{
    config = value;
}

void LoadGenerator::resetStop()
{
    stopRequested.store(false);
}

void LoadGenerator::requestStop()
{
    stopRequested.store(true);
}

bool LoadGenerator::isRunning()
{
    return running.load();
}

void LoadGenerator::generateHighTraffic()
{
    running.store(true);
    std::vector<std::thread> worker_threads;

    for (unsigned int count = 0; count < config.traffic.highThreads; count++)
    {
        worker_threads.emplace_back([]()
                                    {
            httplib::Client client(
                config.network.loadBalancerHost,
                config.network.loadBalancerPort);
            client.set_connection_timeout(
                config.network.connectionTimeoutSeconds, 0);
            client.set_read_timeout(
                config.network.requestTimeoutSeconds, 0);
            client.set_write_timeout(
                config.network.requestTimeoutSeconds, 0);

            json payload;
            payload["value"] = config.traffic.highValue;

            for (int req = 0;
                 req < static_cast<int>(config.traffic.highRequestsPerThread) &&
                 !stopRequested.load();
                 req++) {
                client.Post(
                    config.network.loadBalancerPath,
                    payload.dump(),
                    "application/json");

                if (req + 1 < static_cast<int>(config.traffic.highRequestsPerThread))
                {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(
                            config.traffic.highRequestDelayMilliseconds));
                }
            } });
    }

    for (auto &thread : worker_threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
    running.store(false);
}

void LoadGenerator::generateLowTraffic()
{
    running.store(true);

    httplib::Client client(
        config.network.loadBalancerHost,
        config.network.loadBalancerPort);
    client.set_connection_timeout(
        config.network.connectionTimeoutSeconds, 0);
    client.set_read_timeout(
        config.network.requestTimeoutSeconds, 0);
    client.set_write_timeout(
        config.network.requestTimeoutSeconds, 0);

    json payload;
    payload["value"] = config.traffic.lowValue;

    for (int req = 0;
         req < static_cast<int>(config.traffic.lowRequestsPerThread) &&
         !stopRequested.load();
         req++)
    {
        client.Post(
            config.network.loadBalancerPath,
            payload.dump(),
            "application/json");

        if (req + 1 < static_cast<int>(config.traffic.lowRequestsPerThread))
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(
                    config.traffic.lowRequestDelayMilliseconds));
        }
    }
    running.store(false);
}