#ifndef AUTOSCALER_HPP
#define AUTOSCALER_HPP

#include "VMMonitor.hpp"
#include "VMCloner.hpp"
#include "Config.hpp"

#include <libvirt/libvirt.h>

#include <string>
#include <chrono>
#include <vector>
#include <unordered_map>

class Autoscaler
{
private:
    std::string loadBalancerIP_;
    int loadBalancerPort_;
    virConnectPtr connection_;

    VMMonitor &monitor_;
    VMCloner &cloner_;
    Config config_;

    std::string baseImage_;
    std::string initialVMName_;
    
    unsigned int vcpus_;
    unsigned long memory_;

    unsigned int currentReplicas_;
    unsigned int maxReplicas_;

    unsigned int overloadCount_;
    double cpuThreshold_;
    unsigned int requiredOverloadCount_;

    bool scalingInProgress_;

    std::chrono::steady_clock::time_point startTime_;
    std::chrono::seconds warmupPeriod_;

    std::vector<std::string> activeVMs_;
    std::unordered_map<std::string, unsigned int> vmOverloadCounts_;
    std::unordered_map<std::string, unsigned int> vmLowLoadCounts_;

    double scaleInThreshold_ = 25.0;
    unsigned int requiredLowLoadCount_ = 5;

    std::string getVMIPv4(const std::string &vmName);
    bool waitForServer(const std::string &ip, int port);
    void scaleOut();
    void scaleIn();

public:
    Autoscaler(
        virConnectPtr connection,
        VMMonitor &monitor,
        VMCloner &cloner,
        const Config &config);

    void evaluate(const VMStats &stats);
    void monitorAllVMs();
};

#endif