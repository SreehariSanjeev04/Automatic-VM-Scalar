#ifndef LOADBALANCER_H
#define LOADBALANCER_H

class LoadBalancer {
    private:
        int registryPort;
        int serverPort;
    public:
        LoadBalancer(int registryPort, int serverPort);
        void startRegistryServer();
};

#endif