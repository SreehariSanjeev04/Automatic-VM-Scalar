#include "RegistryServer.hpp"
#include "httplib.h"
#include "Logger.hpp"
#include "VmManager.hpp"

RegistryServer::RegistryServer(int port) : port(port) {}

void RegistryServer::startRegistryServer() {
    httplib::Server svr;

    svr.Post("/register", [](const httplib::Request &req, httplib::Response &res) {
        std::string ip = req.remote_addr;
            
        VmManager::registerVM(ip);

        LOG_INFO("Registered VM with IP %s", ip.c_str());

        res.status = 200;
        res.set_content("{\"status\": \"success\"}", "application/json");
    });


    svr.Post("/deregister", [](const httplib::Request &req, httplib::Response &res) {
        std::string ip = req.remote_addr;
            
        if (VmManager::deregisterVM(ip)) {
            res.status = 200;
            res.set_content("{\"status\": \"success\"}", "application/json");
        } else {
            res.status = 404;
            res.set_content("{\"status\": \"failed\", \"error\": \"VM not registered\"}", "application/json");
        }
    });

    svr.listen("0.0.0.0", port);
}