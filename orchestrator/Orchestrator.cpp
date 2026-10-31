#include "Orchestrator.hpp"
#include "Autoscaler.hpp"
#include "LoadBalancer.hpp"
#include <csignal>
#include <filesystem>
#include <iostream>
#include <thread>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
namespace { volatile std::sig_atomic_t interrupted=0; void signalHandler(int) { interrupted=1; } }
int Orchestrator::run(const char* configPath) {
    int lockFd=-1;
    try {
        auto config=Config::load(configPath);
        std::filesystem::create_directories(std::filesystem::path(config.stateFile).parent_path());
        lockFd=::open((config.stateFile+".lock").c_str(),O_CREAT|O_RDWR,0600);
        if(lockFd<0 || ::flock(lockFd,LOCK_EX|LOCK_NB)<0) throw std::runtime_error("Another orchestrator owns the lifecycle journal");
        std::signal(SIGINT,signalHandler); std::signal(SIGTERM,signalHandler);
        auto lifecycle=makeLibvirtDriver(config),monitor=makeLibvirtDriver(config);
        VMManager registry; Autoscaler scaler(config,registry,*lifecycle);
        scaler.initialize();
        LoadBalancer server(config,registry);
        if(!server.bind()) throw std::runtime_error("Cannot bind load balancer port");
        scaler.start();
        std::thread listener([&]{server.listen();});
        std::cout<<"Ready: "<<config.vm.initialName<<"; listening on "<<config.network.loadBalancerPort<<std::endl;
        try {
            auto next=std::chrono::steady_clock::now();
            while(!interrupted) {
                if(std::chrono::steady_clock::now()>=next) {
                    scaler.tick(*monitor); next=std::chrono::steady_clock::now()+std::chrono::seconds(config.scaling.monitorIntervalSeconds);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        } catch(...) { server.stop();listener.join();scaler.stop();throw; }
        server.stop(); listener.join(); scaler.stop();
        ::close(lockFd); return 0;
    } catch(const std::exception& e) {
        std::cerr<<"[ORCHESTRATOR] "<<e.what()<<std::endl;
        if(lockFd>=0) ::close(lockFd);
        return 1;
    }
}
