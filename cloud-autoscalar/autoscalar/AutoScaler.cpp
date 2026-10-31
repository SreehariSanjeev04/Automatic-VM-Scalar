#include "AutoScaler.hpp"
#include "VmManager.hpp"

#include <iostream>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cstdlib>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

Autoscaler::Autoscaler(
    virConnectPtr connection,
    VMMonitor &monitor,
    VMCloner &cloner,
    const std::string &baseImage,
    unsigned int vcpus,
    unsigned long memory,
    unsigned int initialReplicas,
    unsigned int maxReplicas)
    : connection_(connection),
      monitor_(monitor),
      cloner_(cloner),
      baseImage_(baseImage),
      vcpus_(vcpus),
      memory_(memory),
      currentReplicas_(initialReplicas),
      maxReplicas_(maxReplicas),
      overloadCount_(0),
      cpuThreshold_(75.0),
      requiredOverloadCount_(3),
      scalingInProgress_(false),
      startTime_(std::chrono::steady_clock::now()),
      warmupPeriod_(30)
{

    if (!connection_)
    {
        throw std::runtime_error("Invalid libvirt connection");
    }

    if (initialReplicas == 0 || maxReplicas < initialReplicas)
    {
        throw std::invalid_argument("Invalid replica configuration");
    }

    activeVMs_.push_back("ubuntu-base");
}

std::string Autoscaler::getVMIPv4(const std::string &vmName)
{

    virDomainPtr domain =
        virDomainLookupByName(connection_, vmName.c_str());

    if (!domain)
    {
        throw std::runtime_error("VM not found: " + vmName);
    }

    for (int attempt = 0; attempt < 60; attempt++)
    {

        virDomainInterfacePtr *interfaces = nullptr;

        int count = virDomainInterfaceAddresses(
            domain,
            &interfaces,
            VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_LEASE,
            0);

        if (count >= 0)
        {

            for (int i = 0; i < count; i++)
            {

                for (unsigned int j = 0;
                     j < interfaces[i]->naddrs;
                     j++)
                {

                    if (interfaces[i]->addrs[j].type ==
                        VIR_IP_ADDR_TYPE_IPV4)
                    {

                        std::string ip =
                            interfaces[i]->addrs[j].addr;

                        for (int k = 0; k < count; k++)
                        {
                            virDomainInterfaceFree(interfaces[k]);
                        }

                        std::free(interfaces);
                        virDomainFree(domain);

                        return ip;
                    }
                }
            }

            for (int i = 0; i < count; i++)
            {
                virDomainInterfaceFree(interfaces[i]);
            }

            std::free(interfaces);
        }

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }

    virDomainFree(domain);

    throw std::runtime_error(
        "Could not obtain DHCP IP for " + vmName);
}

bool Autoscaler::waitForServer(
    const std::string &ip,
    int port)
{

    for (int attempt = 0; attempt < 60; attempt++)
    {

        int sock = socket(AF_INET, SOCK_STREAM, 0);

        if (sock < 0)
        {
            throw std::runtime_error("Socket creation failed");
        }

        sockaddr_in address{};

        address.sin_family = AF_INET;
        address.sin_port = htons(port);

        if (inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1)
        {
            close(sock);
            throw std::runtime_error("Invalid server IP: " + ip);
        }

        bool connected =
            connect(
                sock,
                reinterpret_cast<sockaddr *>(&address),
                sizeof(address)) == 0;

        close(sock);

        if (connected)
        {
            return true;
        }

        std::this_thread::sleep_for(
            std::chrono::seconds(1));
    }

    return false;
}

void Autoscaler::evaluate(const VMStats &stats)
{
    if (stats.state != VIR_DOMAIN_RUNNING)
    {
        vmOverloadCounts_[stats.name] = 0;
        return;
    }

    auto elapsed =
        std::chrono::steady_clock::now() - startTime_;

    if (elapsed < warmupPeriod_)
    {
        std::cout << "[AUTOSCALER] Warm-up period. "
                  << "Skipping scaling decisions.\n";
        return;
    }

    std::cout << "[AUTOSCALER] "
              << stats.name
              << " CPU: " << stats.cpuUsage << "%\n";

    if (stats.cpuUsage > cpuThreshold_)
    {
        vmOverloadCounts_[stats.name]++;

        std::cout << "[AUTOSCALER] " << stats.name
                  << " overload count: "
                  << vmOverloadCounts_[stats.name]
                  << "/" << requiredOverloadCount_
                  << '\n';
    }
    else
    {
        vmOverloadCounts_[stats.name] = 0;
    }
}

void Autoscaler::monitorAllVMs()
{
    for (const auto &vmName : activeVMs_)
    {
        try
        {
            VMStats stats = monitor_.monitor(vmName);
            evaluate(stats);
        }
        catch (const std::exception &e)
        {
            std::cerr << "[AUTOSCALER] Failed to monitor "
                      << vmName << ": " << e.what() << '\n';
        }
    }

    if (currentReplicas_ >= maxReplicas_ || scalingInProgress_)
        return;

    bool overloaded = false;

    for (const auto &vmName : activeVMs_)
    {
        if (vmOverloadCounts_[vmName] >= requiredOverloadCount_)
        {
            overloaded = true;

            std::cout << "[AUTOSCALER] Sustained overload detected on "
                      << vmName << '\n';

            break;
        }
    }

    if (overloaded)
    {
        scaleOut();
    }
}

void Autoscaler::scaleOut()
{
    if (currentReplicas_ >= maxReplicas_ || scalingInProgress_)
        return;

    scalingInProgress_ = true;

    std::string newVMName =
        "server-" + std::to_string(currentReplicas_ + 1);

    try
    {
        std::cout << "[AUTOSCALER] Creating "
                  << newVMName << '\n';

        cloner_.clone(
            newVMName,
            baseImage_,
            vcpus_,
            memory_);

        currentReplicas_++;

        std::cout << "[AUTOSCALER] VM created. "
                  << "Discovering IP...\n";

        std::string ip = getVMIPv4(newVMName);

        std::cout << "[AUTOSCALER] "
                  << newVMName << " IP: " << ip << '\n';

        std::cout << "[AUTOSCALER] Waiting for server application...\n";

        if (!waitForServer(ip, 8080))
        {
            throw std::runtime_error(
                "Server application did not become ready");
        }

        std::cout << "[AUTOSCALER] Server is ready. "
                  << "Registering VM...\n";

        VmManager::registerVM(ip);

        activeVMs_.push_back(newVMName);
        vmOverloadCounts_[newVMName] = 0;

        std::cout << "[AUTOSCALER] Registered "
                  << newVMName << " (" << ip << ")\n";

        for (auto &entry : vmOverloadCounts_)
        {
            entry.second = 0;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "[AUTOSCALER] Scaling failed: "
                  << e.what() << '\n';
    }

    scalingInProgress_ = false;
}