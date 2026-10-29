#include "LoadGenerator.hpp"
#include "Client.hpp"
#include "httplib.h"
Client::Client(int port, int mode) : port(port), mode(mode) {}

void Client::startRegistryServer(int port) {
    httplib::Server svr;

    svr.Post("/register", [](const httplib::Request &req, httplib::Response &res) {
        
    });
}