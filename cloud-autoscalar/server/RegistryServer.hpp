#ifndef REGISTRYSERVER_H
#define REGISTRYSERVER_H

class RegistryServer {
    private:
        int port;
    public:
        RegistryServer(int port);
        void startRegistryServer();
};

#endif
