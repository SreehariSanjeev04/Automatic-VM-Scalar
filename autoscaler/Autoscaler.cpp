#include "Autoscaler.hpp"
#include <algorithm>
#include <utility>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>
using json=nlohmann::json;
Autoscaler::Autoscaler(const Config& c,VMManager& registry,Driver& driver)
    : config_(c),registry_(registry),driver_(driver),policy_(c.scaling),
      cpu_(c.metricsDirectory,"cpu.csv","timestamp,name,cpu_percent,valid,state"),
      events_(c.metricsDirectory,"events.csv","timestamp,event,name,detail"),
      legacyCpu_(std::filesystem::path(c.metrics.filePath).parent_path().string(),std::filesystem::path(c.metrics.filePath).filename().string(),"timestamp_ms,vm_name,cpu_percent,replica_count",true) {}
Autoscaler::~Autoscaler() { stop(); }
void Autoscaler::save() {
    json items=json::array();
    for(const auto& r:resources_) items.push_back({{"name",r.name},{"volume",r.volume},{"phase",r.phase}});
    json document={{"owner",owner_},{"next_id",nextId_},{"resources",items}};
    auto path=std::filesystem::path(config_.stateFile);
    std::filesystem::create_directories(path.parent_path());
    auto temporary=path.string()+".tmp";
    {
        std::ofstream out(temporary); out << document.dump(2) << '\n'; out.flush();
        if(!out) throw std::runtime_error("Cannot write lifecycle journal");
    }
    int fd=::open(temporary.c_str(),O_RDONLY);
    if(fd<0 || ::fsync(fd)<0) { if(fd>=0) ::close(fd); throw std::runtime_error("Cannot sync lifecycle journal"); }
    ::close(fd); std::filesystem::rename(temporary,path);
    fd=::open(path.parent_path().c_str(),O_RDONLY|O_DIRECTORY);
    if(fd>=0) { ::fsync(fd); ::close(fd); }
}
std::string Autoscaler::ready(const std::string& name) {
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(config_.scaling.readinessTimeoutSeconds);
    while(!stopped_ && std::chrono::steady_clock::now()<deadline) {
        auto address=driver_.ip(name);
        if(!address.empty() && driver_.healthy(address)) return address;
        std::unique_lock lock(taskMutex_); wake_.wait_for(lock,std::chrono::seconds(1),[&]{return stopped_.load();});
    }
    throw std::runtime_error("Readiness deadline/cancellation: "+name);
}
void Autoscaler::initialize() {
    owner_=driver_.preflight();
    if(std::filesystem::exists(config_.stateFile)) {
        std::ifstream in(config_.stateFile); json document; in>>document;
        if(document.at("owner")!=owner_) throw std::runtime_error("Journal belongs to a different initial VM");
        nextId_=document.at("next_id").get<unsigned>();
        for(const auto& r:document.at("resources")) {
            Resource item{r.at("name"),r.at("volume"),r.at("phase")};
            if(item.name==config_.vm.initialName || item.volume!=item.name+"-"+owner_+".qcow2") throw std::runtime_error("Invalid owned-resource journal");
            resources_.push_back(item);
        }
    }
    registry_.put({config_.vm.initialName,ready(config_.vm.initialName),"ready",false,0});
    for(auto& r:resources_) {
        if(r.phase!="cleanup" && driver_.ownedDomain(r,owner_)) {
            try {
                registry_.put({r.name,ready(r.name),"ready",true,0}); r.phase="ready";
                events_.row({"adopted",r.name,"restart reconciliation"});
                continue;
            } catch(const std::exception& e) { events_.row({"recovery_failed",r.name,e.what()}); }
        }
        r.phase="cleanup"; registry_.put({r.name,"","cleanup",true,0});
    }
    save(); reconcile();
    events_.row({"initial_ready",config_.vm.initialName,owner_});
}
void Autoscaler::eraseResource(const std::string& name) {
    resources_.erase(std::remove_if(resources_.begin(),resources_.end(),[&](const auto& r){return r.name==name;}),resources_.end());
}
void Autoscaler::reconcile() {
    // The lifecycle worker is the sole owner of the journal and lifecycle driver.
    for(const auto& backend:registry_.snapshot()) {
        if(backend.state=="cleanup") {
            auto it=std::find_if(resources_.begin(),resources_.end(),[&](auto& r){return r.name==backend.name;});
            if(it==resources_.end()) continue;
            try {
                driver_.remove(*it,owner_); registry_.erase(backend.name); eraseResource(backend.name); save();
                events_.row({"cleanup_complete",backend.name,""});
            } catch(const std::exception& e) { events_.row({"cleanup_failed",backend.name,e.what()}); }
        } else if(backend.state=="unready") {
            auto address=driver_.ip(backend.name);
            if(driver_.healthy(address) && backend.inFlight==0) {
                registry_.put({backend.name,address,"ready",backend.owned,0});
                ++epoch_; events_.row({"healthy_again",backend.name,address});
            }
        }
    }
}
void Autoscaler::start() { worker_=std::thread([this]{loop();}); }
void Autoscaler::stop() {
    stopped_=true; wake_.notify_all(); if(worker_.joinable()) worker_.join();
}
void Autoscaler::schedule(const std::string& action) {
    std::lock_guard lock(taskMutex_);
    if(stopped_ || busy_.exchange(true)) return;
    task_=action; wake_.notify_all();
}
void Autoscaler::loop() {
    while(!stopped_) {
        std::string action;
        {
            std::unique_lock lock(taskMutex_);
            wake_.wait_for(lock,std::chrono::seconds(2),[&]{return stopped_ || !task_.empty();});
            if(stopped_) break;
            action=std::exchange(task_,{});
        }
        try {
            if(action=="out") scaleOut();
            else if(action=="in") scaleIn();
            else reconcile();
        } catch(const std::exception& e) { events_.row({"lifecycle_error","",e.what()}); std::cerr<<e.what()<<'\n'; }
        if(!action.empty()) { ++epoch_; busy_=false; }
    }
}
void Autoscaler::tick(Driver& monitoring) {
    if(seenEpoch_!=epoch_.load()) { seenEpoch_=epoch_.load(); policy_.reset(); }
    auto backends=registry_.snapshot();
    bool high=false, low=!backends.empty(), readyOnly=true;
    for(const auto& b:backends) {
        bool valid=false; double value=0; int state=0;
        if(b.state=="ready") {
            try {
                auto stats=monitoring.sample(b.name); valid=stats.valid && stats.state==VIR_DOMAIN_RUNNING; value=stats.cpuUsage; state=stats.state;
                if(stats.state!=VIR_DOMAIN_RUNNING) registry_.state(b.name,"unready");
            } catch(const std::exception& e) { registry_.state(b.name,"unready"); events_.row({"sample_error",b.name,e.what()}); }
        }
        policy_.observe(b.name,valid && !busy_,value);
        cpu_.row({b.name,std::to_string(value),valid?"1":"0",std::to_string(state)});
        if(valid) legacyCpu_.row({b.name,std::to_string(value),std::to_string(registry_.membership()["replicas"].size())});
        high=high || policy_.overloaded(b.name);
        low=low && policy_.underloaded(b.name);
        readyOnly=readyOnly && b.state=="ready" && valid;
    }
    if(busy_ || stopped_) return;
    if(high && readyOnly && backends.size()<config_.vm.maxReplicas) schedule("out");
    else if(config_.scaling.scaleInEnabled && low && readyOnly && backends.size()>1) schedule("in");
}
void Autoscaler::scaleOut() {
    if(registry_.snapshot().size()>=config_.vm.maxReplicas || stopped_) return;
    auto name=config_.vm.replicaNamePrefix+std::to_string(nextId_++);
    Resource r{name,name+"-"+owner_+".qcow2","creating"};
    resources_.push_back(r); save(); registry_.put({name,"","provisioning",true,0});
    events_.row({"scale_out_start",name,""});
    try {
        driver_.create(r,owner_);
        auto address=ready(name);
        resources_.back().phase="ready"; save();
        registry_.put({name,address,"ready",true,0});
        events_.row({"scale_out_ready",name,address});
    } catch(const ResourceCollision&) {
        // Nothing was created by this attempt; never clean up the colliding resource.
        registry_.erase(name); eraseResource(name); save(); throw;
    } catch(...) {
        resources_.back().phase="cleanup"; save(); registry_.state(name,"cleanup");
        // Removal verifies ownership; collisions with unrelated domains are never removed.
        try { driver_.remove(r,owner_); registry_.erase(name); eraseResource(name); save(); }
        catch(const std::exception& e) { events_.row({"cleanup_failed",name,e.what()}); }
        throw;
    }
}
void Autoscaler::scaleIn() {
    if(stopped_ || resources_.empty()) return;
    auto it=std::find_if(resources_.rbegin(),resources_.rend(),[](auto& r){return r.phase=="ready";});
    if(it==resources_.rend()) return;
    auto name=it->name; auto resource=*it;
    events_.row({"scale_in_start",name,"draining"});
    if(!registry_.drain(name,std::chrono::seconds(config_.scaling.drainTimeoutSeconds))) {
        auto address=driver_.ip(name);
        registry_.state(name,driver_.healthy(address)?"ready":"unready");
        events_.row({"scale_in_cancelled",name,"drain timeout"}); return;
    }
    if(stopped_) { registry_.state(name,"ready"); return; }
    it->phase="cleanup"; save(); registry_.state(name,"cleanup");
    try {
        driver_.remove(resource,owner_); registry_.erase(name); eraseResource(name); save();
        events_.row({"scale_in_complete",name,""});
    } catch(...) {
        auto address=driver_.ip(name);
        if(driver_.ownedDomain(resource,owner_) && driver_.healthy(address)) {
            it->phase="ready"; save(); registry_.state(name,"ready");
        }
        throw;
    }
}
