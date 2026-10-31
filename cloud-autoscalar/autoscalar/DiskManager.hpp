#ifndef DISKMANAGER_H
#define DISKMANAGER_H

#include <libvirt/libvirt.h>
#include <string>

class DiskManager {
private:
    virConnectPtr conn_;
    std::string poolName_;

public:
    DiskManager(
        virConnectPtr conn,
        const std::string& poolName
    );

    std::string createOverlay(
        const std::string& basePath,
        const std::string& overlayName
    );
};

#endif