#ifndef DOMAINXMLBUILDER_H
#define DOMAINXMLBUILDER_H

#include <string>

class DomainXMLBuilder
{
public:
    static std::string build(
        const std::string &name,
        const std::string &overlayPath,
        unsigned int vcpus,
        unsigned long memoryMiB);
};

#endif