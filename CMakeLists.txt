cmake_minimum_required(VERSION 3.10)
project(vMSC VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add source files
set(SOURCES
    src/vmsc.cpp
    src/call_manager.cpp
    src/mobility_manager.cpp
    src/subscriber_manager.cpp
    main.cpp
)

# Add header files
set(HEADERS
    include/vmsc.h
    include/call_manager.h
    include/mobility_manager.h
    include/subscriber_manager.h
)

# Include directory
include_directories(${CMAKE_SOURCE_DIR}/include)

# Create executable
add_executable(vmsc ${SOURCES} ${HEADERS})

# Set output directory
set_target_properties(vmsc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install target
install(TARGETS vmsc DESTINATION bin)
