#include "Config.hpp"

#include "json.hpp"

#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace
{
template <typename T>
void readValue(
    const json &object,
    const char *key,
    T &target)
{
    if (object.contains(key))
    {
        target = object.at(key).get<T>();
    }
}

void validate(const Config &config)
{
    if (config.libvirt.uri.empty() ||
        config.libvirt.storagePool.empty() ||
        config.libvirt.baseImage.empty() ||
        config.vm.initialName.empty() ||
        config.vm.vcpus == 0 ||
        config.vm.memoryMiB == 0 ||
        config.vm.initialReplicas == 0 ||
        config.vm.maxReplicas < config.vm.initialReplicas ||
        config.vm.guestPort == 0 ||
        config.vm.replicaNamePrefix.empty() ||
        config.vm.workPath.empty() ||
        config.network.loadBalancerHost.empty() ||
        config.network.loadBalancerPort == 0 ||
        config.network.loadBalancerBindAddress.empty() ||
        config.network.loadBalancerPath.empty() ||
        config.network.connectionTimeoutSeconds == 0 ||
        config.network.requestTimeoutSeconds == 0 ||
        config.scaling.monitorIntervalSeconds == 0 ||
        config.scaling.requiredOverloadCount == 0 ||
        config.scaling.requiredLowLoadCount == 0 ||
        config.traffic.highThreads == 0 ||
        config.traffic.highRequestsPerThread == 0 ||
        config.traffic.lowRequestsPerThread == 0 ||
        config.traffic.lowRequestDelayMilliseconds == 0)
    {
        throw std::invalid_argument("autoscaler.config contains invalid values");
    }
}
}

Config Config::load(const std::string &path)
{
    std::ifstream input(path);
    if (!input)
    {
        throw std::runtime_error("Could not open configuration file: " + path);
    }

    json document;
    input >> document;

    Config config;
    const auto &libvirt = document.value("libvirt", json::object());
    const auto &vm = document.value("vm", json::object());
    const auto &scaling = document.value("scaling", json::object());
    const auto &network = document.value("network", json::object());
    const auto &traffic = document.value("traffic", json::object());

    readValue(libvirt, "uri", config.libvirt.uri);
    readValue(libvirt, "storage_pool", config.libvirt.storagePool);
    readValue(libvirt, "base_image", config.libvirt.baseImage);

    readValue(vm, "initial_name", config.vm.initialName);
    readValue(vm, "vcpus", config.vm.vcpus);
    readValue(vm, "memory_mib", config.vm.memoryMiB);
    readValue(vm, "initial_replicas", config.vm.initialReplicas);
    readValue(vm, "max_replicas", config.vm.maxReplicas);
    readValue(vm, "guest_port", config.vm.guestPort);
    readValue(vm, "replica_name_prefix", config.vm.replicaNamePrefix);
    readValue(vm, "work_path", config.vm.workPath);

    readValue(scaling, "scale_out_cpu_threshold", config.scaling.scaleOutCpuThreshold);
    readValue(scaling, "scale_in_cpu_threshold", config.scaling.scaleInCpuThreshold);
    readValue(scaling, "required_overload_count", config.scaling.requiredOverloadCount);
    readValue(scaling, "required_low_load_count", config.scaling.requiredLowLoadCount);
    readValue(scaling, "monitor_interval_seconds", config.scaling.monitorIntervalSeconds);
    readValue(scaling, "warmup_seconds", config.scaling.warmupSeconds);
    readValue(scaling, "cooldownSeconds", config.scaling.cooldownSeconds);

    readValue(network, "load_balancer_host", config.network.loadBalancerHost);
    readValue(network, "load_balancer_port", config.network.loadBalancerPort);
    readValue(network, "load_balancer_bind_address", config.network.loadBalancerBindAddress);
    readValue(network, "load_balancer_path", config.network.loadBalancerPath);
    readValue(network, "connection_timeout_seconds", config.network.connectionTimeoutSeconds);
    readValue(network, "request_timeout_seconds", config.network.requestTimeoutSeconds);

    readValue(traffic, "high_threads", config.traffic.highThreads);
    readValue(traffic, "high_requests_per_thread", config.traffic.highRequestsPerThread);
    readValue(traffic, "high_value", config.traffic.highValue);
    readValue(traffic, "low_requests_per_thread", config.traffic.lowRequestsPerThread);
    readValue(traffic, "low_value", config.traffic.lowValue);
    readValue(traffic, "low_request_delay_milliseconds", config.traffic.lowRequestDelayMilliseconds);

    validate(config);
    return config;
}
