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
    commons/Config.cpp
    autoscaler/DiskManager.cpp
    autoscaler/DomainXMLBuilder.cpp
    autoscaler/LibvirtConnection.cpp
    autoscaler/VMManager.cpp
    autoscaler/VMCloner.cpp
    orchestrator/Orchestrator.cpp
    server/LoadBalancer.cpp
    autoscaler/VMMonitor.cpp
    autoscaler/Autoscaler.cpp
)

target_include_directories(cloud_autoscalar PRIVATE
    autoscaler
    client
    commons
    logger
    orchestrator
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

add_executable(cloud_autoscalar_client
    commons/Config.cpp
    client/Client.cpp
    client/LoadGenerator.cpp
)

target_include_directories(cloud_autoscalar_client PRIVATE
    client
    commons
    logger
)

target_link_libraries(cloud_autoscalar_client PRIVATE
    Threads::Threads
)

if (MSVC)
    target_compile_options(cloud_autoscalar_client PRIVATE /W4)
else()
    target_compile_options(cloud_autoscalar_client PRIVATE -Wall -Wextra -Wpedantic)
endif()
