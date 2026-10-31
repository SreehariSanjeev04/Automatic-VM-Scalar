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

        auto usage = calculate(previous->second.cpuTime, info.cpuTime,
                               previous->second.timestamp, currentTime, stats.vcpus);
        if (usage)
        {
            stats.valid = true;
            stats.cpuUsage = *usage;
        }
    }

    previousReadings_[vmName] = {
        info.cpuTime,
        currentTime};

    virDomainFree(domain);

    return stats;
}
std::optional<double> VMMonitor::calculate(unsigned long long previousCPU, unsigned long long currentCPU,
                                           unsigned long long previousTime, unsigned long long currentTime, unsigned vcpus)
{
    if (!vcpus || currentTime <= previousTime || currentCPU < previousCPU)
        return {};
    return 100.0 * static_cast<double>(currentCPU - previousCPU) /
           (static_cast<double>(currentTime - previousTime) * vcpus);
}
