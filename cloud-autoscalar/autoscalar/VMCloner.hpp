#pragma once

#include "DiskManager.hpp"
#include "DomainXMLBuilder.hpp"
#include <libvirt/libvirt.h>
#include <string>

class VMCloner {
private:
    virConnectPtr conn_;
    DiskManager diskManager_;
    DomainXMLBuilder xmlBuilder_;

public:
    VMCloner(
        virConnectPtr conn,
        const std::string& poolName
    );

    bool clone(
        const std::string& newName,
        const std::string& baseDiskPath,
        unsigned int vcpus,
        unsigned long memoryMiB
    );

    bool remove(const std::string& vmName);
};