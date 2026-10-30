#ifndef LOADBALANCER_H
#define LOADBALANCER_H

#include <mutex>

class LoadBalancer {
    private:
        int port;
        int prevIndex = -1;
        std::mutex index_mutex;
    public:
        LoadBalancer(int port);
        void startLoadBalancer();
        bool distributeWork(int value);
};

#endif