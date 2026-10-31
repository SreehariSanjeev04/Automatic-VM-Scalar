#ifndef VMMANAGER_H
#define VMMANAGER_H

#include <vector>
#include <string>
#include <mutex>
#include <optional>

class VMManager {
    private:    
        static std::vector<std::string> clientIPs;
        static std::mutex registry_mutex;
    public:
        static void registerVM(const std::string& ip);
        static bool deregisterVM(const std::string& ip);
        static std::optional<std::string> getVMIpAtIndex(const int index);
        static int getNumOfVMs();
};

#endif