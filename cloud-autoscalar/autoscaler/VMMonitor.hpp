#ifndef VM_MONITOR_HPP
#define VM_MONITOR_HPP

#include <libvirt/libvirt.h>
#include <string>
#include <unordered_map>
#include <cstdint>

struct VMStats {
    std::string name;
    double cpuUsage;
    unsigned int vcpus;
    int state;
};

class VMMonitor {
private:
    virConnectPtr connection_;

    struct CPUReading {
        unsigned long long cpuTime;
        unsigned long long timestamp;
    };

    std::unordered_map<std::string, CPUReading> previousReadings_;

    unsigned long long getTimestamp();

public:
    explicit VMMonitor(virConnectPtr connection);

    VMStats monitor(const std::string& vmName);
};

#endif