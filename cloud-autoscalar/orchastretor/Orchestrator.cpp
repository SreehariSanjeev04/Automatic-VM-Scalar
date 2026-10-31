#include "Orchestrator.hpp"
#include "Logger.hpp"
#include "LoadBalancer.hpp"
#include "RegistryServer.hpp"
#include "Client.hpp"
#include "Constant.hpp"
#include "LibvirtConnection.hpp"
#include "VMCloner.hpp"

#include <thread>

void Orchestrator::run() {
    // Client client(Mode::high);
    // LoadBalancer loadBalancer(8080);
    // RegistryServer registryServer(8000);
    
    // std::thread clientThread([&client]() {
    //     client.generateTraffic();
    // });

    // std::thread loadBalancerThread([&loadBalancer]() {
    //     loadBalancer.startLoadBalancer();
    // });

    // std::thread registryServerThread([&registryServer]() {
    //     registryServer.startRegistryServer();
    // });

    // clientThread.join();
    // loadBalancerThread.join();
    // registryServerThread.join();
    
    try {
        LibvirtConnection conn;
        VMCloner cloner(
            conn.get(),
            "images"
        );
        if (cloner.clone(
            "vm-2",
            "/mnt/LinuxData/images/base.qcow2",
            1,
            1024
        )) {
            LOG_INFO("VM created successfully");
        }

    } catch (const std::exception& e) {
        LOG_ERROR("Error: %s", e.what());
    }
}