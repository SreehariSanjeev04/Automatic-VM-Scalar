#include "LoadBalancer.hpp"
#include "Logger.hpp"
#include "httplib.h"
#include "VMManager.hpp"
#include "json.hpp"

using json = nlohmann::json;

LoadBalancer::LoadBalancer(const Config &config)
    : port(static_cast<int>(config.network.loadBalancerPort)),
      config_(config) {}

void LoadBalancer::startLoadBalancer()
{
    httplib::Server svr;

    svr.Post(config_.network.loadBalancerPath,
             [this](const httplib::Request &req, httplib::Response &res)
             {
        try {
            std::string body = req.body;

        json data = json::parse(body);

        if (data.contains("value") && data["value"].is_number_integer()) {

            LOG_INFO("Value received: %d", data["value"].get<int>());

            if(!distributeWork(data["value"].get<int>())) {
                res.status = 500;
                res.set_content("{\"status\":\"failed\", \"error\":\"Could not send the traffic\"}", "application/json");
                return;
            }
            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } else {
            res.status = 400;
            res.set_content("{\"status\":\"failed\",\"error\":\"Missing parameter 'value'\"}", "application/json");
        }
        } catch (const json::parse_error& err) {
            res.status = 400;
            res.set_content("{\"status\":\"Malformed JSON\"}", "application/json");
        } });

    svr.listen(config_.network.loadBalancerBindAddress, port);
}

bool LoadBalancer::distributeWork(int value)
{
    int size = VMManager::getNumOfVMs();

    if (size == 0)
    {
        LOG_ERROR("No VMs are available to distribute the traffic.");
        return false;
    }

    int nextIndex;
    {
        std::lock_guard<std::mutex> lock(index_mutex);
        nextIndex = (prevIndex + 1) % size;
    }
    std::optional<std::string> vmIP = VMManager::getVMIpAtIndex(nextIndex);

    if (!vmIP.has_value())
    {
        LOG_ERROR("No VMs are available to distribute the traffic.");
        return false;
    }

    httplib::Client client(vmIP.value(), config_.vm.guestPort);
    client.set_connection_timeout(
        config_.network.connectionTimeoutSeconds, 0);
    client.set_read_timeout(
        config_.network.requestTimeoutSeconds, 0);
    client.set_write_timeout(
        config_.network.requestTimeoutSeconds, 0);

    LOG_INFO("Sending work to VM with IP %s", vmIP.value().c_str());
    if (auto res =     client.Get(
        config_.vm.workPath + "?n=" + std::to_string(value));
        res && res->status >= 200 && res->status < 300)
    {
        LOG_INFO("Response status: %d", res->status);
        std::lock_guard<std::mutex> lock(index_mutex);
        prevIndex = nextIndex;

        return true;
    }
    else
    {
        if (res)
        {
            LOG_ERROR("Backend returned status: %d", res->status);
        }
        else
        {
            LOG_ERROR("Request failed: %s", httplib::to_string(res.error()).c_str());
        }
        return false;
    }
}