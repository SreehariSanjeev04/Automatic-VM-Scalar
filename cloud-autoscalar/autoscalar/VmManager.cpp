#include "VmManager.hpp"
#include "Logger.hpp"

#include <algorithm>

std::vector<std::string> VmManager::clientIPs;
std::mutex VmManager::registry_mutex;

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

int VmManager::getNumOfVMs(){
    std::lock_guard<std::mutex> lock(registry_mutex);
    return static_cast<int>(clientIPs.size());
}

std::optional<std::string> VmManager::getVMIpAtIndex(int index){
    std::lock_guard<std::mutex> lock(registry_mutex);
    if (index < 0 || index >= static_cast<int>(clientIPs.size()))
        return std::nullopt;
    return clientIPs.at(index);
}