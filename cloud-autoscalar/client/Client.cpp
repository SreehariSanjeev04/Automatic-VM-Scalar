#include "LoadGenerator.hpp"
#include "Logger.hpp"

#include <iostream>
#include <string>
#include <thread>

int main()
{
    LOG_INFO("Client started. Commands: increase, decrease, exit");

    std::string command;
    std::thread trafficThread;

    while (true)
    {
        std::cout << "> " << std::flush;

        if (!std::getline(std::cin, command))
        {
            LOG_ERROR("Failed to read a command");
            LoadGenerator::requestStop();
            if (trafficThread.joinable())
            {
                trafficThread.join();
            }
            return 1;
        }

        if (command == "increase")
        {
            if (LoadGenerator::isRunning())
            {
                LOG_ERROR("Traffic generation is already running");
                continue;
            }
            if (trafficThread.joinable())
            {
                trafficThread.join();
            }

            LOG_INFO("Increasing traffic");
            LoadGenerator::resetStop();
            trafficThread = std::thread(
                LoadGenerator::generateHighTraffic);
        }
        else if (command == "decrease")
        {
            if (LoadGenerator::isRunning())
            {
                LOG_ERROR("Traffic generation is already running");
                continue;
            }
            if (trafficThread.joinable())
            {
                trafficThread.join();
            }

            LOG_INFO("Decreasing traffic");
            LoadGenerator::resetStop();
            trafficThread = std::thread(
                LoadGenerator::generateLowTraffic);
        }
        else if (command == "exit")
        {
            LOG_INFO("Client shutting down");
            LoadGenerator::requestStop();
            if (trafficThread.joinable())
            {
                trafficThread.join();
            }
            return 0;
        }
        else
        {
            LOG_ERROR(
                "Unknown command: %s. Use increase, decrease, or exit",
                command.c_str());
        }
    }
}