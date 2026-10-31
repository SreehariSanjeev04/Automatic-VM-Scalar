#ifndef VM_MONITOR_HPP
#define VM_MONITOR_HPP

#include <libvirt/libvirt.h>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <optional>

struct VMStats
{
    std::string name;
    double cpuUsage;
    bool valid = false;
    unsigned int vcpus;
    int state;
};

class VMMonitor
{
private:
    virConnectPtr connection_;

    struct CPUReading
    {
        unsigned long long cpuTime;
        unsigned long long timestamp;
    };

    std::unordered_map<std::string, CPUReading> previousReadings_;

    unsigned long long getTimestamp();

public:
    explicit VMMonitor(virConnectPtr connection);

    static std::optional<double> calculate(unsigned long long previousCPU, unsigned long long currentCPU, unsigned long long previousTime, unsigned long long currentTime, unsigned vcpus);

    VMStats monitor(const std::string &vmName);
};

#endif