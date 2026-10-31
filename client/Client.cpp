#include "LoadGenerator.hpp"
#include "Config.hpp"

#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    const char *configPath = argc > 1 ? argv[1] : "autoscaler.config";

    try
    {
        LoadGenerator::configure(Config::load(configPath));
    }
    catch (const std::exception &)
    {
        return 1;
    }

    std::string command;
    std::thread trafficThread;

    while (true)
    {
        std::cout << "> " << std::flush;

        if (!std::getline(std::cin, command))
        {
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
                continue;
            }
            if (trafficThread.joinable())
            {
                trafficThread.join();
            }

            LoadGenerator::resetStop();
            trafficThread = std::thread(
                LoadGenerator::generateHighTraffic);
        }
        else if (command == "decrease")
        {
            if (LoadGenerator::isRunning())
            {
                continue;
            }
            if (trafficThread.joinable())
            {
                trafficThread.join();
            }

            LoadGenerator::resetStop();
            trafficThread = std::thread(
                LoadGenerator::generateLowTraffic);
        }
        else if (command == "exit")
        {
            LoadGenerator::requestStop();
            if (trafficThread.joinable())
            {
                trafficThread.join();
            }
            return 0;
        }
    }
}