#include "VMMonitor.hpp"

#include <stdexcept>
#include <chrono>

VMMonitor::VMMonitor(virConnectPtr connection)
    : connection_(connection)
{

    if (!connection_)
    {
        throw std::runtime_error("Invalid libvirt connection");
    }
}

unsigned long long VMMonitor::getTimestamp()
{
    auto now = std::chrono::steady_clock::now();

    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               now.time_since_epoch())
        .count();
}

VMStats VMMonitor::monitor(const std::string &vmName)
{

    virDomainPtr domain =
        virDomainLookupByName(connection_, vmName.c_str());

    if (!domain)
    {
        throw std::runtime_error("VM not found: " + vmName);
    }

    virDomainInfo info{};

    if (virDomainGetInfo(domain, &info) < 0)
    {
        virDomainFree(domain);
        throw std::runtime_error("Failed to retrieve VM information");
    }

    unsigned long long currentTime = getTimestamp();

    VMStats stats{};
    stats.name = vmName;
    stats.vcpus = info.nrVirtCpu;
    stats.state = info.state;
    stats.cpuUsage = 0.0;

    if (info.state != VIR_DOMAIN_RUNNING || stats.vcpus == 0)
    {
        previousReadings_.erase(vmName);
        virDomainFree(domain);
        return stats;
    }

    auto previous = previousReadings_.find(vmName);

    if (previous != previousReadings_.end())
    {

        unsigned long long deltaCPU = 0;

        if (info.cpuTime >= previous->second.cpuTime)
        {
            deltaCPU = info.cpuTime - previous->second.cpuTime;
        }

        unsigned long long deltaWall =
            currentTime - previous->second.timestamp;

        if (deltaWall > 0)
        {
            stats.cpuUsage =
                (static_cast<double>(deltaCPU) /
                 (static_cast<double>(deltaWall) * stats.vcpus)) *
                100.0;
        }
    }

    previousReadings_[vmName] = {
        info.cpuTime,
        currentTime};

    virDomainFree(domain);

    return stats;
}