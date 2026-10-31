#include "Orchestrator.hpp"
#include "Logger.hpp"
#include "LoadBalancer.hpp"
#include "RegistryServer.hpp"
#include "Client.hpp"
#include "Constant.hpp"
#include "LibvirtConnection.hpp"
#include "VMCloner.hpp"
#include "VMMonitor.hpp"
#include "AutoScaler.hpp"

#include <thread>
#include <iostream>

void Orchestrator::run()
{
    try {
        LibvirtConnection connection;

        VMMonitor monitor(connection.get());

        VMCloner cloner(
            connection.get(),
            "images"
        );

        LoadBalancer loadBalancer(8080);

        Autoscaler autoscaler(
            connection.get(),
            monitor,
            cloner,
            "/mnt/LinuxData/images/base.qcow2",
            2, 2048
        );

        std::thread loadBalancerThread([&]() {
            loadBalancer.startLoadBalancer();
        });

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
        std::cerr << "[ORCHESTRATOR] " << e.what() << '\n';
        return;
    }

    return;
}