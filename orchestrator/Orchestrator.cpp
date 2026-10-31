#include "Orchestrator.hpp"
#include "Logger.hpp"
#include "LoadBalancer.hpp"
#include "LibvirtConnection.hpp"
#include "VMCloner.hpp"
#include "VMMonitor.hpp"
#include "Autoscaler.hpp"

#include <thread>

void Orchestrator::run(const char *configPath)
{
    try {
        Config config = Config::load(configPath);
        LOG_INFO("Loaded configuration from %s", configPath);

        LOG_INFO("Connecting to libvirt");
        LibvirtConnection connection(config.libvirt.uri.c_str());

        if (!connection.get())
        {
            LOG_ERROR("Cannot start orchestrator without a libvirt connection");
            return;
        }

        LOG_INFO("Connected to libvirt");
        VMMonitor monitor(connection.get());

        LOG_INFO("Initializing VM cloner");
        VMCloner cloner(
            connection.get(),
            config.libvirt.storagePool
        );

        LOG_INFO("Initializing autoscaler");
        LoadBalancer loadBalancer(config);

        Autoscaler autoscaler(
            connection.get(),
            monitor,
            cloner,
            config
        );

        LOG_INFO("Autoscaler initialized");
        LOG_INFO(
            "Starting load balancer on port %u",
            config.network.loadBalancerPort);
        std::thread loadBalancerThread([&]() {
            loadBalancer.startLoadBalancer();
        });

        LOG_INFO("Starting autoscaler");
        std::thread autoscalerThread([&]() {
            while (true) {
                autoscaler.monitorAllVMs();
                std::this_thread::sleep_for(
                    std::chrono::seconds(config.scaling.monitorIntervalSeconds));
            }
        });
        loadBalancerThread.join();
        autoscalerThread.join();
    }
    catch (const std::exception& e) {
        LOG_ERROR("Orchestrator failed: %s", e.what());
        return;
    }

    return;
}