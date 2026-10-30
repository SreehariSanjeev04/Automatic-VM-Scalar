#ifndef CLIENT_H
#define CLIENT_h

#include "LoadGenerator.hpp"

class Client {
    private:
        int mode;
    public:
        Client(int mode);
        void generateTraffic();
};

#endif