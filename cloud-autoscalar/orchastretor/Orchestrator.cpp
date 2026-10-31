#include "Orchestrator.hpp"
#include "Logger.hpp"
#include "LoadBalancer.hpp"
#include "LibvirtConnection.hpp"
#include "VMCloner.hpp"
#include "VMMonitor.hpp"
#include "AutoScaler.hpp"

#include <thread>

void Orchestrator::run()
{
    try {
        LOG_INFO("Connecting to libvirt");
        LibvirtConnection connection;

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
            "images"
        );

        LOG_INFO("Initializing autoscaler");
        LoadBalancer loadBalancer(8080);

        Autoscaler autoscaler(
            connection.get(),
            monitor,
            cloner,
            "/mnt/LinuxData/images/base.qcow2",
            2, 2048
        );

        LOG_INFO("Autoscaler initialized");
        LOG_INFO("Starting load balancer on port %d", 8080);
        std::thread loadBalancerThread([&]() {
            loadBalancer.startLoadBalancer();
        });

        LOG_INFO("Starting autoscaler");
        std::thread autoscalerThread([&]() {
            while (true) {
                autoscaler.monitorAllVMs();
                std::this_thread::sleep_for(std::chrono::seconds(2));
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