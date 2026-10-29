#ifndef VMMANAGER_H
#define VMMANAGER_H

#include <vector>
#include <string>
#include <mutex>

class VmManager {
    private:    
        std::vector<std::string> clientIPs;
        std::mutex registry_mutex;
    public:
        virtual void registerVM(const std::string& ip); 
        virtual void deregisterVM(const std::string& ip);
};

#endif