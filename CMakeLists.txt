cmake_minimum_required(VERSION 3.14)
project(vMSC LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(OSMO REQUIRED libosmocore libosmogsm libosmoabis talloc)

add_executable(vmsc main.cpp)

target_include_directories(vmsc PRIVATE ${OSMO_INCLUDE_DIRS})

target_link_options(vmsc PRIVATE -Wl,--no-as-needed)

target_link_libraries(vmsc PRIVATE 
    osmogsm
    osmoisdn
    osmoabis
    osmocore
    talloc
    mnl
)

target_compile_options(vmsc PRIVATE -Wall -g)