#pragma once

#include <string>

struct Config
{
    struct Libvirt
    {
        std::string uri = "qemu:///system";
        std::string storagePool = "default";
        std::string baseImage = "/var/lib/libvirt/images/autoscaler-base.qcow2";
    } libvirt;

    struct VM
    {
        std::string initialName = "ubuntu_base";
        unsigned int vcpus = 1;
        unsigned long memoryMiB = 1024;
        unsigned int initialReplicas = 1;
        unsigned int maxReplicas = 2;
        unsigned int guestPort = 8080;
        std::string replicaNamePrefix = "vm-";
        std::string workPath = "/binary-trees";
    } vm;

    struct Scaling
    {
        double scaleOutCpuThreshold = 75.0;
        double scaleInCpuThreshold = 25.0;
        unsigned int requiredOverloadCount = 3;
        unsigned int requiredLowLoadCount = 5;
        unsigned int monitorIntervalSeconds = 2;
        unsigned int warmupSeconds = 30;
        unsigned int cooldownSeconds = 30;
        unsigned int drainTimeoutSeconds = 60;
        unsigned int readinessTimeoutSeconds = 120;
        bool scaleInEnabled = true;
    } scaling;

    struct Network
    {
        std::string loadBalancerHost = "192.168.122.1";
        unsigned int loadBalancerPort = 8080;
        std::string loadBalancerBindAddress = "0.0.0.0";
        std::string loadBalancerPath = "/work";
        unsigned int connectionTimeoutSeconds = 2;
        unsigned int requestTimeoutSeconds = 30;
        unsigned int clientTimeoutSeconds = 35;
    } network;

    struct Traffic
    {
        unsigned int highThreads = 8;
        unsigned int highRequestsPerThread = 1;
        unsigned int highRequestDelayMilliseconds = 1000;
        int highValue = 14;
        double highRate = 8.0;
        double lowRate = 0.8;
        unsigned int queueCapacity = 32;
        unsigned int lowRequestsPerThread = 1;
        int lowValue = 14;
        unsigned int lowRequestDelayMilliseconds = 2000;
    } traffic;

    struct Metrics
    {
        std::string filePath = "results/vm_metrics.csv";
    } metrics;
    std::string stateFile = ".autoscaler-state.json";
    std::string metricsDirectory = "runs/current";
    static Config load(const std::string &path);
};
