#ifndef LIBVIRTCONNECTION_H
#define LIBVIRTCONNECTION_H

#include <libvirt/libvirt.h>
#include <stdexcept>

class LibvirtConnection {
private:
    virConnectPtr conn_;

public:
    explicit LibvirtConnection(const char* uri);

    ~LibvirtConnection();

    LibvirtConnection(const LibvirtConnection&) = delete;
    LibvirtConnection& operator=(const LibvirtConnection&) = delete;

    virConnectPtr get() const;
};

#endif