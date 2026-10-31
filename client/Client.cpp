#include "LoadGenerator.hpp"
#include <iostream>
int main(int argc,char** argv) {
    try {
        LoadGenerator traffic(Config::load(argc>1?argv[1]:"autoscaler.config"));
        std::cout<<"Commands: increase, decrease, stop, exit"<<std::endl;
        std::string command;
        while(std::getline(std::cin,command)) {
            if(command=="exit") break;
            if(command=="increase" || command=="decrease" || command=="stop")traffic.mode(command);
            else std::cerr<<"Unknown command"<<std::endl;
        }
        traffic.stop();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
}
