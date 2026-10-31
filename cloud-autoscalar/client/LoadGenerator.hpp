#ifndef LOADGENERATOR_H
#define LOADGENERATOR_H

class LoadGenerator {
    public: 
        static void generateHighTraffic();
        static void generateLowTraffic();
        static void resetStop();
        static void requestStop();
        static bool isRunning();
};

#endif
