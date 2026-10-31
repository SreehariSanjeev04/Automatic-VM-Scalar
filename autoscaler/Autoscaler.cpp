#include "Autoscaler.hpp"
#include "Logger.hpp"
#include "VMManager.hpp"

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
    const Config &config)
    : connection_(connection),
      monitor_(monitor),
      cloner_(cloner),
      config_(config),
      baseImage_(config.libvirt.baseImage),
      vcpus_(config.vm.vcpus),
      memory_(config.vm.memoryMiB),
      currentReplicas_(config.vm.initialReplicas),
      maxReplicas_(config.vm.maxReplicas),
      overloadCount_(0),
      cpuThreshold_(config.scaling.scaleOutCpuThreshold),
      requiredOverloadCount_(config.scaling.requiredOverloadCount),
      scalingInProgress_(false),
      startTime_(std::chrono::steady_clock::now()),
      warmupPeriod_(config.scaling.warmupSeconds),
      scaleInThreshold_(config.scaling.scaleInCpuThreshold),
      requiredLowLoadCount_(config.scaling.requiredLowLoadCount),
      lastScaleTime_(std::chrono::steady_clock::now()),
      cooldownPeriod_(config.scaling.cooldownSeconds)
{

    if (!connection_)
    {
        throw std::runtime_error("Invalid libvirt connection");
    }

    initialVMName_ = config.vm.initialName;

    LOG_INFO("Discovering IP address for initial VM: %s", initialVMName_.c_str());
    std::string ip = getVMIPv4(initialVMName_);

    LOG_INFO(
        "Waiting for initial VM server at %s:%u",
        ip.c_str(),
        config.vm.guestPort);
    if (!waitForServer(ip, config.vm.guestPort))
    {
        throw std::runtime_error(
            "Initial VM server application is not ready: " + initialVMName_);
    }

    VMManager::registerVM(ip);
    activeVMs_.push_back(initialVMName_);
    vmOverloadCounts_[initialVMName_] = 0;
    vmLowLoadCounts_[initialVMName_] = 0;

    startTime_ = std::chrono::steady_clock::now() - warmupPeriod_;

    LOG_INFO("Initial VM registered: %s", initialVMName_.c_str());
    LOG_INFO("Warm-up skipped for initial VM");
}

std::string Autoscaler::getVMIPv4(const std::string &vmName)
{

    virDomainPtr domain =
        virDomainLookupByName(connection_, vmName.c_str());

    if (!domain)
    {
        throw std::runtime_error("VM not found: " + vmName);
    }

    LOG_INFO("Waiting for DHCP lease for VM: %s", vmName.c_str());

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

    LOG_ERROR("Timed out waiting for DHCP lease for VM: %s", vmName.c_str());
    throw std::runtime_error(
        "Could not obtain DHCP IP for " + vmName);
}

bool Autoscaler::waitForServer(
    const std::string &ip,
    int port)
{
    LOG_INFO("Checking server readiness at %s:%d", ip.c_str(), port);

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

    LOG_ERROR("Timed out waiting for server at %s:%d", ip.c_str(), port);
    return false;
}

void Autoscaler::evaluate(const VMStats &stats)
{
    if (stats.state != VIR_DOMAIN_RUNNING)
    {
        vmOverloadCounts_[stats.name] = 0;
        vmLowLoadCounts_[stats.name] = 0;
        return;
    }

    auto elapsed = std::chrono::steady_clock::now() - startTime_;

    if (elapsed < warmupPeriod_)
    {
        return;
    }

    LOG_INFO("%s CPU: %.2f%%", stats.name.c_str(), stats.cpuUsage);

    if (stats.cpuUsage > cpuThreshold_)
    {
        if (vmOverloadCounts_[stats.name] < requiredOverloadCount_)
        {
            vmOverloadCounts_[stats.name]++;
        }
        vmLowLoadCounts_[stats.name] = 0;

        LOG_INFO(
            "%s overload count: %u/%u",
            stats.name.c_str(),
            vmOverloadCounts_[stats.name],
            requiredOverloadCount_);
    }
    else if (stats.cpuUsage < config_.scaling.scaleInCpuThreshold)
    {
        if (vmLowLoadCounts_[stats.name] < requiredLowLoadCount_)
        {
            vmLowLoadCounts_[stats.name]++;
        }
        vmOverloadCounts_[stats.name] = 0;

        LOG_INFO(
            "%s low-load count: %u/%u",
            stats.name.c_str(),
            vmLowLoadCounts_[stats.name],
            requiredLowLoadCount_);
    }
    else
    {
        vmOverloadCounts_[stats.name] = 0;
        vmLowLoadCounts_[stats.name] = 0;
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
            LOG_ERROR("Failed to monitor %s: %s", vmName.c_str(), e.what());
        }
    }

    if (scalingInProgress_)
    {
        return;
    }

    if (hasScaled_)
    {
        auto elapsed = std::chrono::steady_clock::now() - lastScaleTime_;

        if (elapsed < cooldownPeriod_)
        {
            LOG_INFO("Scaling cooldown active");
            return;
        }
    }

    // Scale-out decision
    if (currentReplicas_ < maxReplicas_)
    {
        for (const auto &vmName : activeVMs_)
        {
            if (vmOverloadCounts_[vmName] >= requiredOverloadCount_)
            {
                LOG_INFO("Sustained overload detected");
                scaleOut();
                return;
            }
        }
    }

    // Scale-in decision
    if (currentReplicas_ <= 1)
    {
        return;
    }

    bool allVMsUnderloaded = true;

    for (const auto &vmName : activeVMs_)
    {
        if (vmLowLoadCounts_[vmName] < requiredLowLoadCount_)
        {
            allVMsUnderloaded = false;
            break;
        }
    }

    if (allVMsUnderloaded)
    {
        LOG_INFO("Sustained low load detected");
        scaleIn();
    }
}

void Autoscaler::scaleOut()
{
    if (currentReplicas_ >= maxReplicas_ || scalingInProgress_)
        return;

    scalingInProgress_ = true;

    std::string newVMName =
        config_.vm.replicaNamePrefix +
        std::to_string(currentReplicas_ + 1);

    try
    {
        LOG_INFO("Creating %s", newVMName.c_str());

        if (!cloner_.clone(
                newVMName,
                baseImage_,
                vcpus_,
                memory_))
        {
            LOG_ERROR("Failed to clone VM: %s", newVMName.c_str());
            return;
        }

        currentReplicas_++;

        LOG_INFO("VM created. Discovering IP");

        std::string ip = getVMIPv4(newVMName);

        LOG_INFO("%s IP: %s", newVMName.c_str(), ip.c_str());

        LOG_INFO("Waiting for server application");

        if (!waitForServer(ip, config_.vm.guestPort))
        {
            throw std::runtime_error(
                "Server application did not become ready");
        }

        LOG_INFO("Server is ready. Registering VM");

        VMManager::registerVM(ip);

        activeVMs_.push_back(newVMName);
        vmOverloadCounts_[newVMName] = 0;
        vmLowLoadCounts_[newVMName] = 0;

        for (auto &entry : vmOverloadCounts_)
        {
            entry.second = 0;
        }

        for (auto &entry : vmLowLoadCounts_)
        {
            entry.second = 0;
        }

        LOG_INFO("Registered %s (%s)", newVMName.c_str(), ip.c_str());

        lastScaleTime_ = std::chrono::steady_clock::now();
        hasScaled_ = true;

        for (auto &entry : vmOverloadCounts_)
        {
            entry.second = 0;
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Scaling failed: %s", e.what());
    }

    scalingInProgress_ = false;
}

void Autoscaler::scaleIn()
{
    if (currentReplicas_ <= 1 || scalingInProgress_)
    {
        return;
    }

    scalingInProgress_ = true;

    if (activeVMs_.size() <= 1)
    {
        LOG_INFO("Only the initial VM remains");
        scalingInProgress_ = false;
        return;
    }

    std::string vmName = activeVMs_.back();

    if (vmName == initialVMName_)
    {
        LOG_ERROR("Refusing to remove the initial VM");
        scalingInProgress_ = false;
        return;
    }

    try
    {
        LOG_INFO("Scaling in. Removing %s", vmName.c_str());

        std::string ip = getVMIPv4(vmName);

        // Stop sending new requests to this VM.
        bool wasRegistered = VMManager::deregisterVM(ip);

        if (!wasRegistered)
        {
            LOG_INFO(
                "VM %s was already absent from the load balancer registry",
                ip.c_str());
        }

        // Stop and undefine the VM, then delete its overlay.
        if (!cloner_.remove(vmName))
        {
            if (wasRegistered)
            {
                VMManager::registerVM(ip);
            }

            throw std::runtime_error(
                "Failed to remove VM: " + vmName);
        }

        activeVMs_.pop_back();

        vmOverloadCounts_.erase(vmName);
        vmLowLoadCounts_.erase(vmName);

        currentReplicas_--;

        lastScaleTime_ = std::chrono::steady_clock::now();
        hasScaled_ = true;

        LOG_INFO("Successfully removed %s", vmName.c_str());
        LOG_INFO("Active replicas: %u", currentReplicas_);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Scale-in failed: %s", e.what());
    }

    scalingInProgress_ = false;
}