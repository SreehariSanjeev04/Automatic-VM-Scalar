#include "DiskManager.hpp"
#include "Logger.hpp"

#include <sstream>
#include <cstdlib>

DiskManager::DiskManager(
    virConnectPtr conn,
    const std::string &poolName) : conn_(conn), poolName_(poolName) {}

std::string DiskManager::createOverlay(
    const std::string &basePath,
    const std::string &overlayName)
{
    virStoragePoolPtr pool =
        virStoragePoolLookupByName(
            conn_, poolName_.c_str());

    if (!pool)
    {
        LOG_ERROR("Storage pool not found - %s", poolName_.c_str());
        return {};
    }

    virStorageVolPtr base =
        virStorageVolLookupByPath(
            conn_, basePath.c_str());

    if (!base)
    {
        virStoragePoolFree(pool);

        LOG_ERROR(
            "Base image is not a managed libvirt volume: %s", basePath.c_str());
        return {};
    }

    virStorageVolInfo info{};

    if (virStorageVolGetInfo(base, &info) < 0)
    {
        virStorageVolFree(base);
        virStoragePoolFree(pool);

        LOG_ERROR(
            "Failed to inspect base image");
        return {};
    }

    std::ostringstream xml;

    xml << "<volume>"
        << "<name>" << overlayName << "</name>"
        << "<capacity unit='bytes'>"
        << info.capacity
        << "</capacity>"
        << "<allocation unit='bytes'>0</allocation>"
        << "<target>"
        << "<format type='qcow2'/>"
        << "</target>"
        << "<backingStore>"
        << "<path>" << basePath << "</path>"
        << "<format type='qcow2'/>"
        << "</backingStore>"
        << "</volume>";

    virStorageVolPtr overlay =
        virStorageVolCreateXML(
            pool,
            xml.str().c_str(),
            0);

    virStorageVolFree(base);
    virStoragePoolFree(pool);

    if (!overlay)
    {
        LOG_ERROR("Failed to create QCOW2 overlay");
        return {};
    }

    char *path = virStorageVolGetPath(overlay);

    if (!path)
    {
        virStorageVolFree(overlay);

        LOG_ERROR("Failed to retrieve overlay path");
        return {};
    }

    std::string result(path);

    free(path);
    virStorageVolFree(overlay);

    return result;
}