#ifndef LOADGENERATOR_H
#define LOADGENERATOR_H

#include "Config.hpp"

class LoadGenerator {
    public: 
        static void configure(const Config &config);
        static void generateHighTraffic();
        static void generateLowTraffic();
        static void resetStop();
        static void requestStop();
        static bool isRunning();
};

#endif
