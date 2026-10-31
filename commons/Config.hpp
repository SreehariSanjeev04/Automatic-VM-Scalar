#pragma once

#include <string>

struct Config
{
    struct Libvirt
    {
        std::string uri = "qemu:///system";
        std::string storagePool = "images";
        std::string baseImage = "/mnt/LinuxData/images/base.qcow2";
    } libvirt;

    struct VM
    {
        std::string initialName = "vm";
        unsigned int vcpus = 2;
        unsigned long memoryMiB = 2048;
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
    } scaling;

    struct Network
    {
        std::string loadBalancerHost = "192.168.122.1";
        unsigned int loadBalancerPort = 8080;
        std::string loadBalancerBindAddress = "0.0.0.0";
        std::string loadBalancerPath = "/work";
        unsigned int connectionTimeoutSeconds = 2;
        unsigned int requestTimeoutSeconds = 30;
    } network;

    struct Traffic
    {
        unsigned int highThreads = 1;
        unsigned int highRequestsPerThread = 1;
        int highValue = 20;
        unsigned int lowRequestsPerThread = 1;
        int lowValue = 2;
        unsigned int lowRequestDelayMilliseconds = 2000;
    } traffic;

    static Config load(const std::string &path);
};
