#include "VmManager.hpp"
#include "Logger.hpp"

#include <algorithm>

void VmManager::registerVM(const std::string &ip)
{
    std::lock_guard<std::mutex> lock(registry_mutex);

    for (const std::string &clientIp : clientIPs)
    {
        if (clientIp == ip)
            return;
    }

    clientIPs.push_back(ip);

    LOG_INFO("Registered VM with IP address %s\n", ip.c_str());
}

void VmManager::deregisterVM(const std::string &ip)
{
    std::lock_guard<std::mutex> lock(registry_mutex);

    auto it = std::find(clientIPs.begin(), clientIPs.end(), ip);

    if (it == clientIPs.end())
    {
        LOG_ERROR("Could not find VM with IP address %s\n", ip.c_str());
        return;
    }
    else
    {
        clientIPs.erase(it);
    }
    LOG_INFO("Removed VM with IP address %s\n", ip.c_str());
}
