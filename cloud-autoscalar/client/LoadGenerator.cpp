#include "LoadGenerator.hpp"
#include "Logger.hpp"
#include "Constant.hpp"

#include <httplib.h>

void LoadGenerator::generateHighTraffic()
{
    std::vector<std::thread> worker_threads;

    LOG_INFO("Generating high traffic with %d threads\n", Request::num_threads);

    for (int count = 0; count < Request::num_threads; count++)
    {
        worker_threads.emplace_back([]()
                                    {
            httplib::Client client("http://192.168.122.124:8080");
            client.set_connection_timeout(2, 0);

            for(int req = 0; req < Request::requests_per_threads_high; req++) {
                if (auto res = client.Get("/binary-trees?10")) {
                    LOG_INFO("Response status: %d\n", res->status);
                } else {
                    LOG_ERROR("Request failed: %s\n", httplib::to_string(res.error()));
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
}

void LoadGenerator::generateLowTraffic()
{

    LOG_INFO("Generating low traffic");
    httplib::Client client("http://192.168.122.124:8080");
    client.set_connection_timeout(2, 0);

    for (int req = 0; req < Request::requests_per_threads_low; req++)
    {
        if (auto res = client.Get("/binary-trees?2"))
        {
            LOG_INFO("Response status: %d\n", res->status);
        }
        else
        {
            LOG_ERROR("Request failed: %s\n", httplib::to_string(res.error()));
        }

        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}