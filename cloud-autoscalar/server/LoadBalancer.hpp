#ifndef LOADBALANCER_H
#define LOADBALANCER_H

#include <mutex>
#include "Config.hpp"

class LoadBalancer {
    private:
        int port;
        int prevIndex = -1;
        std::mutex index_mutex;
    public:
        explicit LoadBalancer(const Config &config);
        Config config_;
        void startLoadBalancer();
        bool distributeWork(int value);
};

#endif