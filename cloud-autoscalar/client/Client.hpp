#ifndef CLIENT_H
#define CLIENT_h

#include "LoadGenerator.hpp"

class Client {
    private:
        int mode;
        int port;
    public:
        Client(int port, int mode);
        void startRegistryServer(int port);
};

#endif