#include "DomainXMLBuilder.hpp"
#include "Logger.hpp"

#include <sstream>

namespace
{
std::string escapeXml(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (const char character : value)
    {
        switch (character)
        {
        case '&':
            escaped += "&amp;";
            break;
        case '\'':
            escaped += "&apos;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '>':
            escaped += "&gt;";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}
}

std::string DomainXMLBuilder::build(
    const std::string &name,
    const std::string &overlayPath,
    unsigned int vcpus,
    unsigned long memoryMiB)
{
    if (name.empty() || overlayPath.empty() || vcpus == 0 || memoryMiB == 0)
    {
        LOG_ERROR("Invalid VM definition parameters");
        return {};
    }

    std::ostringstream xml;
    xml << "<domain type='kvm'>"
        << "<name>" << escapeXml(name) << "</name>"
        << "<memory unit='MiB'>" << memoryMiB << "</memory>"
        << "<currentMemory unit='MiB'>" << memoryMiB << "</currentMemory>"
        << "<vcpu placement='static'>" << vcpus << "</vcpu>"
        << "<os>"
        << "<type arch='x86_64' machine='q35'>hvm</type>"
        << "<boot dev='hd'/>"
        << "</os>"
        << "<features><acpi/><apic/></features>"
        << "<clock offset='utc'/>"
        << "<on_poweroff>destroy</on_poweroff>"
        << "<on_reboot>restart</on_reboot>"
        << "<on_crash>destroy</on_crash>"
        << "<devices>"
        << "<disk type='file' device='disk'>"
        << "<driver name='qemu' type='qcow2'/>"
        << "<source file='" << escapeXml(overlayPath) << "'/>"
        << "<target dev='vda' bus='virtio'/>"
        << "</disk>"
        << "<interface type='network'>"
        << "<source network='default'/>"
        << "<model type='virtio'/>"
        << "</interface>"
        << "<console type='pty'/>"
        << "<graphics type='spice' autoport='yes'/>"
        << "</devices>"
        << "</domain>";

    return xml.str();
}
