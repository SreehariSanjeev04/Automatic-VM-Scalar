#include "LibvirtConnection.hpp"
#include "Logger.hpp"

LibvirtConnection::LibvirtConnection(const char *uri)
    : conn_(virConnectOpen(uri))
{
    if (!conn_)
    {
        LOG_ERROR(
            "Failed to connect to libvirt");
    }
}

LibvirtConnection::~LibvirtConnection()
{
    if (conn_)
    {
        virConnectClose(conn_);
    }
}

virConnectPtr LibvirtConnection::get() const
{
    return conn_;
}