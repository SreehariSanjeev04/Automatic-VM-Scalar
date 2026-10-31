cmake_minimum_required(VERSION 3.16)

project(cloud_autoscalar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBVIRT REQUIRED IMPORTED_TARGET libvirt)

add_executable(cloud_autoscalar
    main.cpp
    autoscalar/DiskManager.cpp
    autoscalar/DomainXMLBuilder.cpp
    autoscalar/LibvirtConnection.cpp
    autoscalar/VmManager.cpp
    autoscalar/VMCloner.cpp
    client/Client.cpp
    client/LoadGenerator.cpp
    orchastretor/Orchestrator.cpp
    server/LoadBalancer.cpp
    server/RegistryServer.cpp
)

target_include_directories(cloud_autoscalar PRIVATE
    autoscalar
    client
    commons
    logger
    orchastretor
    server
)

target_link_libraries(cloud_autoscalar PRIVATE
    Threads::Threads
    PkgConfig::LIBVIRT
)

if (MSVC)
    target_compile_options(cloud_autoscalar PRIVATE /W4)
else()
    target_compile_options(cloud_autoscalar PRIVATE -Wall -Wextra -Wpedantic)
endif()
