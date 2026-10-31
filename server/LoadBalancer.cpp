#include "LoadBalancer.hpp"
#include "json.hpp"
#include <chrono>
#include <filesystem>
using json=nlohmann::json;
LoadBalancer::LoadBalancer(const Config& c,VMManager& registry)
    : config_(c),registry_(registry),requests_(c.metricsDirectory,"proxy.csv","timestamp,backend,status,latency_ms,error"),
      legacyRequests_(std::filesystem::path(c.metrics.filePath).parent_path().string(),"request_metrics.csv","timestamp_ms,backend_ip,latency_ms,success,http_status",true) {
    server_.new_task_queue=[] { return new httplib::ThreadPool(16,16,128); };
    server_.Get("/health",[](const auto&,auto& res){res.set_content("{\"status\":\"ok\"}","application/json");});
    server_.Get("/replicas",[this](const auto&,auto& res){res.set_content(registry_.membership().dump(),"application/json");});
    server_.Post(config_.network.loadBalancerPath,[this](const httplib::Request& req,httplib::Response& res){
        auto start=std::chrono::steady_clock::now(); std::string backend,error,address;
        auto fail=[&](int code,const std::string& why) { res.status=code;error=why;res.set_content(json({{"status","failed"},{"error",why}}).dump(),"application/json"); };
        try {
            auto data=json::parse(req.body);
            if(!data.is_object() || !data.contains("value") || !data["value"].is_number_integer() || data["value"]<0 || data["value"]>100) {
                fail(400,"Expected integer value from 0 to 100");
            } else {
                auto target=registry_.acquire();
                if(!target) fail(503,"No ready backends");
                else {
                    backend=target->name; address=target->ip; RequestLease lease(registry_,backend);
                    httplib::Client client(target->ip,config_.vm.guestPort);
                    client.set_connection_timeout(config_.network.connectionTimeoutSeconds,0);
                    client.set_read_timeout(config_.network.requestTimeoutSeconds,0);
                    client.set_write_timeout(config_.network.requestTimeoutSeconds,0);
                    auto result=client.Get(config_.vm.workPath+"?n="+std::to_string(data["value"].get<int>()));
                    if(result && result->status>=200 && result->status<300) {
                        res.set_content(json({{"status","success"},{"backend",backend},{"result",result->body}}).dump(),"application/json");
                    } else if(result) fail(result->status,"Backend: "+result->body);
                    else {
                        if(result.error()==httplib::Error::Connection) registry_.state(backend,"unready");
                        fail(result.error()==httplib::Error::Read?504:502,httplib::to_string(result.error()));
                    }
                }
            }
        } catch(const json::exception&) { fail(400,"Malformed JSON or invalid value"); }
        catch(const std::exception& e) { fail(500,e.what()); }
        auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        requests_.row({backend,std::to_string(res.status),std::to_string(elapsed),error});
        legacyRequests_.row({address,std::to_string(elapsed),res.status>=200 && res.status<300?"1":"0",std::to_string(res.status)});
    });
}
bool LoadBalancer::bind() { return server_.bind_to_port(config_.network.loadBalancerBindAddress,config_.network.loadBalancerPort); }
void LoadBalancer::listen() { server_.listen_after_bind(); }
void LoadBalancer::stop() { server_.stop(); }
