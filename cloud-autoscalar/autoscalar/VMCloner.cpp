#include "VMCloner.hpp"
#include "Logger.hpp"

VMCloner::VMCloner(
    virConnectPtr conn,
    const std::string& poolName
)
    : conn_(conn),
      diskManager_(conn, poolName) {}

bool VMCloner::clone(
    const std::string& newName,
    const std::string& baseDiskPath,
    unsigned int vcpus,
    unsigned long memoryMiB
) {
    if (!conn_)
    {
        LOG_ERROR("Cannot clone VM without a libvirt connection");
        return false;
    }

    virDomainPtr existing =
        virDomainLookupByName(
            conn_, newName.c_str()
        );

    if (existing) {
        virDomainFree(existing);

        LOG_ERROR("Domain already exists: %s", newName.c_str());
        return false;
    }

    const std::string overlayName =
        newName + ".qcow2";

    const std::string overlayPath =
        diskManager_.createOverlay(
            baseDiskPath, overlayName
        );
    if (overlayPath.empty()) {
        LOG_ERROR("Failed to create overlay for domain: %s", newName.c_str());
        return false;
    }

    const std::string domainXML =
        xmlBuilder_.build(
            newName,
            overlayPath,
            vcpus,
            memoryMiB
        );
    if (domainXML.empty()) {
        LOG_ERROR("Failed to build XML for domain: %s", newName.c_str());
        return false;
    }

    virDomainPtr domain =
        virDomainDefineXML(
            conn_, domainXML.c_str()
        );

    if (!domain) {
        LOG_ERROR("Failed to define new domain: %s", newName.c_str());
        return false;
    }

    if (virDomainCreate(domain) < 0) {
        virDomainUndefine(domain);
        virDomainFree(domain);

        LOG_ERROR("Failed to start new domain: %s", newName.c_str());
        return false;
    }

    virDomainFree(domain);
    return true;
}

bool VMCloner::remove(const std::string& vmName)
{
    if (!conn_) {
        LOG_ERROR("Cannot remove VM without a libvirt connection");
        return false;
    }

    virDomainPtr domain =
        virDomainLookupByName(conn_, vmName.c_str());

    if (!domain) {
        LOG_ERROR("Domain not found: %s", vmName.c_str());
        return false;
    }

    int state;
    int reason;

    if (virDomainGetState(domain, &state, &reason, 0) < 0) {
        LOG_ERROR("Failed to get domain state: %s", vmName.c_str());
        virDomainFree(domain);
        return false;
    }

    if (state == VIR_DOMAIN_RUNNING) {
        if (virDomainDestroy(domain) < 0) {
            LOG_ERROR("Failed to stop domain: %s", vmName.c_str());
            virDomainFree(domain);
            return false;
        }
    }

    if (virDomainUndefine(domain) < 0) {
        LOG_ERROR("Failed to undefine domain: %s", vmName.c_str());
        virDomainFree(domain);
        return false;
    }

    virDomainFree(domain);

    std::string overlayName = vmName + ".qcow2";

    if (!diskManager_.deleteOverlay(overlayName)) {
        LOG_ERROR("VM removed, but overlay deletion failed: %s",
                  overlayName.c_str());
        return true;
    }

    LOG_INFO("Successfully removed VM: %s", vmName.c_str());
    return true;
}