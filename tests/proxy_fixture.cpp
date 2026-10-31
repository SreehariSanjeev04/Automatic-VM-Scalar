#include "LoadBalancer.hpp"
#include <iostream>
#include <thread>
int main(int argc,char** argv) {
    auto c=Config::load(argv[1]);VMManager registry;
    registry.put({"a","127.0.0.1","ready",false,0});registry.put({"b","127.0.0.2","ready",true,0});
    LoadBalancer server(c,registry);if(!server.bind())return 1;
    std::thread listener([&]{server.listen();});
    std::string command;while(std::getline(std::cin,command) && command!="exit") {
        if(command=="drain") {registry.drain("b",std::chrono::seconds(5));registry.erase("b");}
    }
    server.stop();listener.join();return 0;
}
