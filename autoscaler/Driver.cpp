#include "Driver.hpp"
#include "LibvirtConnection.hpp"
#include "DomainXMLBuilder.hpp"
#include "httplib.h"
#include <libvirt/virterror.h>
#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
namespace {
constexpr const char* metadataNamespace = "urn:cloud-autoscalar:managed";
std::string xml(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch(c) { case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '\'':out+="&apos;";break;case '"':out+="&quot;";break;default:out+=c; }
    }
    return out;
}
template<class T, int (*Free)(T*)> using Handle = std::unique_ptr<T, decltype(Free)>;
using Domain = Handle<virDomain,virDomainFree>;
using Pool = Handle<virStoragePool,virStoragePoolFree>;
using Volume = Handle<virStorageVol,virStorageVolFree>;
std::runtime_error failure(const std::string& message) {
    auto* error=virGetLastError(); return std::runtime_error(message + (error && error->message ? ": " + std::string(error->message) : ""));
}
class LibvirtDriver : public Driver {
    Config config_;
    LibvirtConnection connection_;
    VMMonitor monitor_;
    virConnectPtr conn() { return connection_.get(); }
    Pool pool() {
        Pool p(virStoragePoolLookupByName(conn(),config_.libvirt.storagePool.c_str()),virStoragePoolFree);
        if (!p) throw failure("Storage pool unavailable");
        return p;
    }
    std::string volumePath(virStorageVolPtr volume) {
        std::unique_ptr<char,decltype(&std::free)> path(virStorageVolGetPath(volume),&std::free);
        if (!path) throw failure("Volume path unavailable");
        return path.get();
    }
    bool marked(virDomainPtr domain, const std::string& owner) {
        std::unique_ptr<char,decltype(&std::free)> value(virDomainGetMetadata(domain,VIR_DOMAIN_METADATA_ELEMENT,metadataNamespace,0),&std::free);
        return value && std::string(value.get()).find(owner)!=std::string::npos;
    }
    void checkVolume(const Resource& r, const std::string& owner) {
        if (r.name == config_.vm.initialName || r.volume != r.name + "-" + owner + ".qcow2")
            throw std::runtime_error("Refusing unowned resource: " + r.name);
    }
public:
    explicit LibvirtDriver(const Config& c) : config_(c),connection_(c.libvirt.uri.c_str()),monitor_(connection_.get()) {}
    std::string preflight() override {
        Domain initial(virDomainLookupByName(conn(),config_.vm.initialName.c_str()),virDomainFree);
        virDomainInfo info{};
        if (!initial || virDomainGetInfo(initial.get(),&info)<0 || info.state!=VIR_DOMAIN_RUNNING) throw failure("Initial VM must be running");
        if (info.nrVirtCpu!=config_.vm.vcpus || info.maxMem!=config_.vm.memoryMiB*1024) throw std::runtime_error("Initial VM resources disagree with configuration");
        char uuid[VIR_UUID_STRING_BUFLEN];
        if (virDomainGetUUIDString(initial.get(),uuid)<0) throw failure("Initial VM UUID unavailable");
        auto p=pool(); if (virStoragePoolIsActive(p.get())!=1) throw std::runtime_error("Storage pool must be active");
        Handle<virNetwork,virNetworkFree> network(virNetworkLookupByName(conn(),"default"),virNetworkFree);
        if (!network || virNetworkIsActive(network.get())!=1) throw failure("Default network must be active");
        Volume base(virStorageVolLookupByPath(conn(),config_.libvirt.baseImage.c_str()),virStorageVolFree);
        if (!base) throw failure("Managed base image unavailable");
        std::unique_ptr<char,decltype(&std::free)> baseXml(virStorageVolGetXMLDesc(base.get(),0),&std::free);
        if (!baseXml || std::string(baseXml.get()).find("qcow2")==std::string::npos) throw std::runtime_error("Base must use QCOW2 format");
        virDomainPtr* domains=nullptr;
        int count=virConnectListAllDomains(conn(),&domains,VIR_CONNECT_LIST_DOMAINS_ACTIVE);
        if(count<0) throw failure("Cannot inspect active domain disks");
        bool used=false;
        for(int i=0;i<count;++i) {
            std::unique_ptr<char,decltype(&std::free)> description(virDomainGetXMLDesc(domains[i],0),&std::free);
            // Backing files occur inside backingStore, so only inspect each direct disk source.
            if(description) {
                std::string text=description.get(); std::size_t begin=0;
                while((begin=text.find("<disk ",begin))!=std::string::npos) {
                    auto end=text.find("</disk>",begin); auto source=text.find("<source ",begin);
                    if(source!=std::string::npos && source<end) {
                        auto sourceEnd=text.find("/>",source);
                        if(text.substr(source,sourceEnd-source).find(xml(config_.libvirt.baseImage))!=std::string::npos) used=true;
                    }
                    begin=end==std::string::npos?text.size():end+7;
                }
            }
            virDomainFree(domains[i]);
        }
        std::free(domains);
        if(used) throw std::runtime_error("Backing image is attached as a writable disk to a running VM");
        return uuid;
    }
    bool exists(const std::string& name) override {
        Domain d(virDomainLookupByName(conn(),name.c_str()),virDomainFree); return bool(d);
    }
    bool ownedDomain(const Resource& r,const std::string& owner) override {
        checkVolume(r,owner);
        Domain d(virDomainLookupByName(conn(),r.name.c_str()),virDomainFree);
        return d && marked(d.get(),owner);
    }
    void create(const Resource& r,const std::string& owner) override {
        checkVolume(r,owner);
        if(exists(r.name)) throw ResourceCollision("Domain already exists: " + r.name);
        auto p=pool();
        Volume old(virStorageVolLookupByName(p.get(),r.volume.c_str()),virStorageVolFree);
        if(old) throw ResourceCollision("Volume already exists: " + r.volume);
        Volume base(virStorageVolLookupByPath(conn(),config_.libvirt.baseImage.c_str()),virStorageVolFree);
        virStorageVolInfo info{};
        if(!base || virStorageVolGetInfo(base.get(),&info)<0) throw failure("Cannot inspect base");
        auto text="<volume><name>"+xml(r.volume)+"</name><capacity unit='bytes'>"+std::to_string(info.capacity)+"</capacity><allocation>0</allocation><target><format type='qcow2'/></target><backingStore><path>"+xml(config_.libvirt.baseImage)+"</path><format type='qcow2'/></backingStore></volume>";
        Volume overlay(virStorageVolCreateXML(p.get(),text.c_str(),0),virStorageVolFree);
        if(!overlay) throw failure("Cannot create overlay");
        try {
            auto definition=DomainXMLBuilder::build(r.name,volumePath(overlay.get()),config_.vm.vcpus,config_.vm.memoryMiB);
            auto offset=definition.find("<name>");
            definition.insert(offset,"<metadata><managed xmlns='"+std::string(metadataNamespace)+"' owner='"+xml(owner)+"'/></metadata>");
            Domain d(virDomainDefineXML(conn(),definition.c_str()),virDomainFree);
            if(!d) throw failure("Cannot define domain");
            if(virDomainCreate(d.get())<0) { virDomainUndefine(d.get()); throw failure("Cannot start domain"); }
        } catch(...) { virStorageVolDelete(overlay.get(),0); throw; }
    }
    void remove(const Resource& r,const std::string& owner) override {
        checkVolume(r,owner); auto p=pool();
        Domain d(virDomainLookupByName(conn(),r.name.c_str()),virDomainFree);
        Volume v(virStorageVolLookupByName(p.get(),r.volume.c_str()),virStorageVolFree);
        if(d) {
            if(!marked(d.get(),owner)) throw std::runtime_error("Refusing unowned domain: "+r.name);
            if(v) {
                std::unique_ptr<char,decltype(&std::free)> definition(virDomainGetXMLDesc(d.get(),0),&std::free);
                if(!definition || std::string(definition.get()).find(xml(volumePath(v.get())))==std::string::npos) throw std::runtime_error("Owned disk mismatch");
            }
            if(virDomainIsActive(d.get())==1 && virDomainDestroy(d.get())<0) throw failure("Cannot stop replica");
            if(virDomainUndefine(d.get())<0) throw failure("Cannot undefine replica");
        }
        if(v) {
            if(volumePath(v.get())==config_.libvirt.baseImage) throw std::runtime_error("Refusing to delete base");
            if(virStorageVolDelete(v.get(),0)<0) throw failure("Cannot delete overlay");
        }
    }
    std::string ip(const std::string& name) override {
        Domain d(virDomainLookupByName(conn(),name.c_str()),virDomainFree);
        if(!d) return {};
        virDomainInterfacePtr* interfaces=nullptr;
        int count=virDomainInterfaceAddresses(d.get(),&interfaces,VIR_DOMAIN_INTERFACE_ADDRESSES_SRC_LEASE,0);
        std::vector<std::string> candidates;
        for(int i=0;i<count;++i) {
            for(unsigned j=0;j<interfaces[i]->naddrs;++j)
                if(interfaces[i]->addrs[j].type==VIR_IP_ADDR_TYPE_IPV4) {
                    std::string address=interfaces[i]->addrs[j].addr;
                    if(std::find(candidates.begin(),candidates.end(),address)==candidates.end()) candidates.push_back(address);
                }
            virDomainInterfaceFree(interfaces[i]);
        }
        std::free(interfaces);
        return selectHealthyAddress(candidates,[this](const auto& address){return healthy(address);});
    }
    bool healthy(const std::string& address) override {
        if(address.empty()) return false;
        httplib::Client client(address,config_.vm.guestPort);
        client.set_connection_timeout(config_.network.connectionTimeoutSeconds,0);
        client.set_read_timeout(5,0); client.set_write_timeout(5,0);
        auto result=client.Get("/health");
        return result && result->status==200 && result->body.find("catalan")!=std::string::npos;
    }
    VMStats sample(const std::string& name) override { return monitor_.monitor(name); }
};
}
std::unique_ptr<Driver> makeLibvirtDriver(const Config& config) { return std::make_unique<LibvirtDriver>(config); }
