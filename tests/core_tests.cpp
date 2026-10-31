#include "Autoscaler.hpp"
#include "Catalan.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
#include <unistd.h>
#define CHECK(condition) do { if(!(condition)) throw std::runtime_error(std::string("Check failed at line ")+std::to_string(__LINE__)+": " #condition); } while(false)
struct FakeDriver : Driver {
    bool collision=false; unsigned removals=0;
    bool createFailure=false, readinessFailure=false, removeFailure=false, partialRemoveFailure=false;
    std::set<std::string> domains, volumes;
    std::string preflight() override {return "test-owner";}
    void create(const Resource& r,const std::string&) override {
        if(collision) throw ResourceCollision("injected unowned collision");
        if(createFailure) throw std::runtime_error("injected create failure");
        domains.insert(r.name);volumes.insert(r.volume);
    }
    bool ownedDomain(const Resource& r,const std::string&) override {return domains.count(r.name);}
    bool exists(const std::string& name) override {return domains.count(name);}
    void remove(const Resource& r,const std::string&) override {
        ++removals;
        if(removeFailure)throw std::runtime_error("injected stop failure");
        domains.erase(r.name);
        if(partialRemoveFailure)throw std::runtime_error("injected overlay deletion failure");
        volumes.erase(r.volume);
    }
    std::string ip(const std::string& name) override {return name;}
    bool healthy(const std::string& address) override {return address=="ubuntu_base" || (!readinessFailure && domains.count(address));}
    VMStats sample(const std::string& name) override {return {name,50,true,1,VIR_DOMAIN_RUNNING};}
};
Config configuration(const std::filesystem::path& path) {
    Config c;c.stateFile=(path/"state.json").string();c.metricsDirectory=(path/"metrics").string();
    c.metrics.filePath=(path/"legacy"/"vm_metrics.csv").string();
    c.scaling.warmupSeconds=0;c.scaling.cooldownSeconds=0;c.scaling.readinessTimeoutSeconds=1;c.scaling.drainTimeoutSeconds=1;return c;
}
void policyTests() {
    CHECK(selectHealthyAddress({"stale", "current"}, [](const auto& address) { return address == "current"; }) == "current");
    CHECK(selectHealthyAddress({"stale"}, [](const auto&) { return false; }).empty());
    CHECK(VMMonitor::calculate(0,500,0,1000,2).value()==25.0);
    CHECK(!VMMonitor::calculate(500,0,0,1000,1));
    CHECK(!VMMonitor::calculate(0,500,1000,1000,1));
    CHECK(!VMMonitor::calculate(0,500,0,1000,0));
    Config c;c.scaling.warmupSeconds=0;c.scaling.cooldownSeconds=0;Policy p(c.scaling);
    p.observe("a",true,90);p.observe("a",true,90);CHECK(!p.overloaded("a"));
    p.observe("a",false,90);p.observe("a",true,90);CHECK(!p.overloaded("a"));
    p.observe("a",true,90);p.observe("a",true,90);CHECK(p.overloaded("a"));
    p.observe("a",true,75);CHECK(!p.overloaded("a"));
    for(int i=0;i<5;++i)p.observe("a",true,10);CHECK(p.underloaded("a"));
    p.observe("a",false,0);CHECK(!p.underloaded("a"));
    c.scaling.warmupSeconds=1;Policy warm(c.scaling);
    for(int i=0;i<20;++i)warm.observe("a",true,99);CHECK(!warm.overloaded("a"));
}
void registryTests() {
    VMManager r;r.put({"a","ip1","ready",false,0});r.put({"b","ip2","ready",true,0});
    std::atomic_int a=0,b=0;std::vector<std::thread> threads;
    for(int i=0;i<8;++i)threads.emplace_back([&]{for(int j=0;j<1000;++j){auto item=r.acquire();CHECK(item);RequestLease lease(r,item->name);if(item->name=="a")++a;else ++b;}});
    for(auto& t:threads)t.join();CHECK(a==4000 && b==4000);
    auto held=r.acquire();CHECK(held);
    auto draining=std::async(std::launch::async,[&]{return r.drain(held->name,std::chrono::seconds(1));});
    for(int i=0;i<100 && r.membership()["replicas"].size()!=1;++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto other=r.acquire();CHECK(other && other->name!=held->name);r.release(other->name);
    r.release(held->name);CHECK(draining.get());r.erase(held->name);
    auto membership=r.membership();CHECK(membership["replicas"].size()==1);
}
void lifecycleTests(const std::filesystem::path& path) {
    auto c=configuration(path);FakeDriver driver;VMManager registry;
    {
        Autoscaler a(c,registry,driver);a.initialize();
        driver.collision=true;try{a.scaleOut();CHECK(false);}catch(const ResourceCollision&){}
        CHECK(driver.removals==0 && registry.snapshot().size()==1);
        driver.collision=false;
        driver.createFailure=true;try{a.scaleOut();CHECK(false);}catch(const std::runtime_error&){}
        CHECK(registry.snapshot().size()==1 && driver.domains.empty());
        driver.createFailure=false;driver.readinessFailure=true;
        try{a.scaleOut();CHECK(false);}catch(const std::runtime_error&){}
        CHECK(registry.snapshot().size()==1 && driver.volumes.empty());
        driver.readinessFailure=false;a.scaleOut();CHECK(registry.snapshot().size()==2);
        driver.removeFailure=true;
        try{a.scaleIn();CHECK(false);}catch(const std::runtime_error&){}
        CHECK(registry.membership()["replicas"].size()==2);
        driver.removeFailure=false;
    }
    VMManager restored;
    {
        Autoscaler a(c,restored,driver);a.initialize();CHECK(restored.membership()["replicas"].size()==2);
        auto held=restored.acquire();if(!held->owned){restored.release(held->name);held=restored.acquire();}CHECK(held->owned);
        a.scaleIn();CHECK(restored.membership()["replicas"].size()==2); // Drain deadline restores healthy replica.
        restored.release(held->name);
        driver.partialRemoveFailure=true;
        try{a.scaleIn();CHECK(false);}catch(const std::runtime_error&){}
        CHECK(restored.membership()["replicas"].size()==1 && !driver.volumes.empty());
    }
    driver.partialRemoveFailure=false;VMManager recovered;
    {Autoscaler a(c,recovered,driver);a.initialize();CHECK(recovered.snapshot().size()==1);CHECK(driver.volumes.empty());}
}
void configTests(const std::filesystem::path& path) {
    auto file=path/"config.json";
    for(auto text:{R"({"vm":{"vcpus":-1}})",R"({"vm":{"guest_port":65536}})",R"({"vm":{"initial_replicas":2}})",R"({"traffic":{"high_threads":9999999999999}})",R"({"scaling":{"scale_in_cpu_threshold":90}})",R"({"network":{"client_timeout_seconds":20}})",R"({"vm":[]})"}) {
        {std::ofstream out(file);out<<text;}
        bool rejected=false;try{Config::load(file);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    }
    {std::ofstream out(file);out<<"{}";}
    auto c=Config::load(file);CHECK(c.vm.vcpus==1 && c.vm.memoryMiB==1024 && c.vm.maxReplicas==2);
    {std::ofstream out(file);out << R"({"scaling":{"scale_in_enabled":false,"cooldownSeconds":10},"traffic":{"high_threads":10,"high_request_delay_milliseconds":500,"low_request_delay_milliseconds":1000},"metrics":{"file_path":"custom.csv"}})";}
    c=Config::load(file);CHECK(!c.scaling.scaleInEnabled && c.scaling.cooldownSeconds==10);
    CHECK(c.traffic.highRate==20 && c.traffic.lowRate==1 && c.metrics.filePath==(path/"custom.csv").string());
}
void naturalTests() {
    CHECK(countBinaryTreesNaive(0).str()=="1");CHECK(countBinaryTreesNaive(10).str()=="16796");
    std::vector<Natural> dp(37);dp[0]=Natural(1);
    for(int n=1;n<=36;++n)for(int i=0;i<n;++i)dp[n]+=dp[i]*dp[n-1-i];
    CHECK(dp[36].str()=="11959798385860453492");
}
int main() {
    auto path=std::filesystem::temp_directory_path()/("autoscaler-tests-"+std::to_string(getpid()));
    std::filesystem::create_directories(path);
    try {policyTests();registryTests();configTests(path);naturalTests();lifecycleTests(path);std::filesystem::remove_all(path);std::cout<<"All core tests passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<"\nArtifacts: "<<path<<'\n';return 1;}
}
