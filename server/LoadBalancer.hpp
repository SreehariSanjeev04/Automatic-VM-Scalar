#pragma once
#include "Config.hpp"
#include "VMManager.hpp"
#include "Metrics.hpp"
#include "httplib.h"
class LoadBalancer
{
    Config config_;
    VMManager &registry_;
    Csv requests_, legacyRequests_;
    httplib::Server server_;

public:
    LoadBalancer(const Config &, VMManager &);
    bool bind();
    void listen();
    void stop();
};
