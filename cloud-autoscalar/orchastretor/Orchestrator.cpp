#include "Orchestrator.hpp"
#include "Logger.hpp"
#include "LoadBalancer.hpp"
#include "RegistryServer.hpp"
#include "Client.hpp"
#include "Constant.hpp"

#include <thread>

void Orchestrator::run() {
    Client client(Mode::high);
    LoadBalancer loadBalancer(8080);
    RegistryServer registryServer(8000);
    
    std::thread clientThread([&client]() {
        client.generateTraffic();
    });

    std::thread loadBalancerThread([&loadBalancer]() {
        loadBalancer.startLoadBalancer();
    });

    std::thread registryServerThread([&registryServer]() {
        registryServer.startRegistryServer();
    });

    clientThread.join();
    loadBalancerThread.join();
    registryServerThread.join();
    
}