#include "LoadGenerator.hpp"
#include "Logger.hpp"
#include "Constant.hpp"
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

    LOG_INFO("Generating high traffic with %d threads", Request::num_threads);

    for (int count = 0; count < Request::num_threads; count++)
    {
        worker_threads.emplace_back([]()
                                    {
            httplib::Client client("192.168.122.1", 8080);
            client.set_connection_timeout(2, 0);
            client.set_read_timeout(2, 0);
            client.set_write_timeout(2, 0);

            json payload;
            payload["value"] = 20;

            for (int req = 0;
                 req < Request::requests_per_threads_high &&
                 !stopRequested.load();
                 req++) {
                if (auto res = client.Post("/work", payload.dump(), "application/json")) {
                    if (res->status >= 200 && res->status < 300) {
                        LOG_INFO("Request successful. Status: %d", res->status);
                    } else {
                        LOG_ERROR("Request failed with HTTP status: %d", res->status);
                    }
                } else {
                    LOG_ERROR("Request failed: %s",
                    httplib::to_string(res.error()).c_str());
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

    LOG_INFO("Generating low traffic");
    httplib::Client client("http://192.168.122.1:8080");
    client.set_connection_timeout(2, 0);
    client.set_read_timeout(2, 0);
    client.set_write_timeout(2, 0);

    json payload;
    payload["value"] = 2;

    for (int req = 0;
         req < Request::requests_per_threads_low &&
         !stopRequested.load();
         req++)
    {
        if (auto res = client.Post("/work", payload.dump(), "application/json")) {
                if (res->status >= 200 && res->status < 300) {
                    LOG_INFO("Request successful. Status: %d", res->status);
                } else {
                    LOG_ERROR("Request failed with HTTP status: %d", res->status);
                }
            } else {
                LOG_ERROR("Request failed: %s",
                        httplib::to_string(res.error()).c_str());
            }

        for (int wait = 0; wait < 20 && !stopRequested.load(); wait++)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    running.store(false);
}