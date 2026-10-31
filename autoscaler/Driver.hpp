#pragma once
#include "Config.hpp"
#include "VMMonitor.hpp"
#include <memory>
#include <stdexcept>
#include <functional>
#include <vector>
#include <string>
struct ResourceCollision : std::runtime_error
{
    using std::runtime_error::runtime_error;
};
struct Resource
{
    std::string name, volume, phase;
};
class Driver
{
public:
    virtual ~Driver() = default;
    virtual std::string preflight() = 0;
    virtual void create(const Resource &, const std::string &owner) = 0;
    virtual bool ownedDomain(const Resource &, const std::string &owner) = 0;
    virtual bool exists(const std::string &name) = 0;
    virtual void remove(const Resource &, const std::string &owner) = 0;
    virtual std::string ip(const std::string &name) = 0;
    virtual bool healthy(const std::string &ip) = 0;
    virtual VMStats sample(const std::string &name) = 0;
};
inline std::string selectHealthyAddress(const std::vector<std::string> &candidates,
                                        const std::function<bool(const std::string &)> &healthy)
{
    for (const auto &address : candidates)
        if (healthy(address))
            return address;
    return {};
}
std::unique_ptr<Driver> makeLibvirtDriver(const Config &);
