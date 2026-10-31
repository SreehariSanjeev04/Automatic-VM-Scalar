#include "Config.hpp"

#include "json.hpp"

#include <fstream>
#include <stdexcept>
#include <filesystem>
#include <cmath>
#include <limits>
#include <type_traits>

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
            const auto &value = object.at(key);
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
            {
                if (!value.is_number_integer() ||
                    (value.is_number_unsigned() && value.get<unsigned long long>() > static_cast<unsigned long long>(std::numeric_limits<T>::max())) ||
                    (!value.is_number_unsigned() && (value.get<long long>() < static_cast<long long>(std::numeric_limits<T>::lowest()) ||
                                                     (value.get<long long>() >= 0 && static_cast<unsigned long long>(value.get<long long>()) > static_cast<unsigned long long>(std::numeric_limits<T>::max())))))
                    throw std::invalid_argument(std::string("Invalid integer: ") + key);
            }
            if constexpr (std::is_floating_point_v<T>)
            {
                if (!value.is_number())
                    throw std::invalid_argument(std::string("Invalid number: ") + key);
            }
            target = value.get<T>();
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
            config.vm.initialReplicas != 1 ||
            config.vm.maxReplicas < config.vm.initialReplicas ||
            config.vm.guestPort == 0 || config.vm.guestPort > 65535 ||
            config.vm.replicaNamePrefix.empty() ||
            config.vm.workPath.empty() ||
            config.network.loadBalancerHost.empty() ||
            config.network.loadBalancerPort == 0 || config.network.loadBalancerPort > 65535 ||
            config.network.loadBalancerBindAddress.empty() ||
            config.network.loadBalancerPath.empty() ||
            config.network.connectionTimeoutSeconds == 0 ||
            config.network.requestTimeoutSeconds == 0 ||
            config.network.clientTimeoutSeconds <= config.network.requestTimeoutSeconds ||
            !std::isfinite(config.scaling.scaleOutCpuThreshold) || !std::isfinite(config.scaling.scaleInCpuThreshold) ||
            config.scaling.scaleInCpuThreshold < 0 || config.scaling.scaleInCpuThreshold >= config.scaling.scaleOutCpuThreshold || config.scaling.scaleOutCpuThreshold > 100 ||
            config.scaling.drainTimeoutSeconds == 0 || config.scaling.readinessTimeoutSeconds == 0 ||
            !std::isfinite(config.traffic.highRate) || !std::isfinite(config.traffic.lowRate) ||
            config.traffic.lowRate <= 0 || config.traffic.highRate <= config.traffic.lowRate ||
            config.traffic.queueCapacity == 0 || config.traffic.highThreads > 128 ||
            config.traffic.highValue < 0 || config.traffic.highValue > 100 || config.traffic.lowValue < 0 || config.traffic.lowValue > 100 ||
            config.vm.workPath.front() != '/' || config.network.loadBalancerPath.front() != '/' ||
            config.stateFile.empty() || config.metricsDirectory.empty() ||
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

    if (!document.is_object())
        throw std::invalid_argument("Configuration must be an object");
    for (const auto *section : {"libvirt", "vm", "scaling", "network", "traffic", "metrics"})
        if (document.contains(section) && !document.at(section).is_object())
            throw std::invalid_argument(std::string("Invalid section: ") + section);
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

    readValue(document, "state_file", config.stateFile);
    readValue(document, "metrics_directory", config.metricsDirectory);
    readValue(scaling, "cooldown_seconds", config.scaling.cooldownSeconds);
    readValue(scaling, "drain_timeout_seconds", config.scaling.drainTimeoutSeconds);
    readValue(scaling, "readiness_timeout_seconds", config.scaling.readinessTimeoutSeconds);
    // Backward-compatible keys from the pulled cooldown/metrics commits.
    readValue(scaling, "enable_scale_in", config.scaling.scaleInEnabled);
    readValue(scaling, "scale_in_enabled", config.scaling.scaleInEnabled);
    if (scaling.contains("enable_scale_in") && scaling.contains("scale_in_enabled") && scaling["enable_scale_in"] != scaling["scale_in_enabled"])
        throw std::invalid_argument("Conflicting scale-in settings");
    if (scaling.contains("cooldownSeconds") && scaling.contains("cooldown_seconds") && scaling["cooldownSeconds"] != scaling["cooldown_seconds"])
        throw std::invalid_argument("Conflicting cooldown settings");
    if (!scaling.contains("cooldown_seconds"))
        readValue(scaling, "cooldownSeconds", config.scaling.cooldownSeconds);
    readValue(traffic, "high_request_delay_milliseconds", config.traffic.highRequestDelayMilliseconds);
    if (!traffic.contains("high_rate") && traffic.contains("high_request_delay_milliseconds") && config.traffic.highRequestDelayMilliseconds)
        config.traffic.highRate = 1000.0 * config.traffic.highThreads / config.traffic.highRequestDelayMilliseconds;
    if (!traffic.contains("low_rate") && traffic.contains("low_request_delay_milliseconds") && config.traffic.lowRequestDelayMilliseconds)
        config.traffic.lowRate = 1000.0 / config.traffic.lowRequestDelayMilliseconds;
    readValue(document.value("metrics", json::object()), "file_path", config.metrics.filePath);
    readValue(network, "client_timeout_seconds", config.network.clientTimeoutSeconds);
    readValue(traffic, "high_rate", config.traffic.highRate);
    readValue(traffic, "low_rate", config.traffic.lowRate);
    readValue(traffic, "queue_capacity", config.traffic.queueCapacity);
    if (config.stateFile.empty() || config.metricsDirectory.empty() || config.metrics.filePath.empty())
        throw std::invalid_argument("State and metrics paths must not be empty");
    const auto directory = std::filesystem::absolute(path).parent_path();
    if (std::filesystem::path(config.stateFile).is_relative())
        config.stateFile = (directory / config.stateFile).string();
    if (std::filesystem::path(config.metricsDirectory).is_relative())
        config.metricsDirectory = (directory / config.metricsDirectory).string();
    if (std::filesystem::path(config.metrics.filePath).is_relative())
        config.metrics.filePath = (directory / config.metrics.filePath).string();
    validate(config);
    return config;
}
