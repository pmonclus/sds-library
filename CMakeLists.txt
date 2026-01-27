cmake_minimum_required(VERSION 3.14)
project(sds C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(SDS_BUILD_TESTS "Build tests" ON)
option(SDS_BUILD_EXAMPLES "Build examples" ON)

# Find Paho MQTT C library
find_library(PAHO_MQTT_LIB 
    NAMES paho-mqtt3c
    HINTS /usr/local/lib /opt/homebrew/lib
)

find_path(PAHO_MQTT_INCLUDE 
    NAMES MQTTClient.h
    HINTS /usr/local/include /opt/homebrew/include
)

if(NOT PAHO_MQTT_LIB)
    message(FATAL_ERROR "Paho MQTT C library not found. Install with:\n"
                        "  macOS: brew install eclipse-paho-mqtt-c\n"
                        "  Ubuntu: apt-get install libpaho-mqtt-dev")
endif()

message(STATUS "Found Paho MQTT: ${PAHO_MQTT_LIB}")
message(STATUS "Paho MQTT includes: ${PAHO_MQTT_INCLUDE}")

# SDS Library
add_library(sds STATIC
    src/sds_core.c
    src/sds_json.c
    platform/posix/sds_platform_posix.c
)

target_include_directories(sds 
    PUBLIC include
    PRIVATE ${PAHO_MQTT_INCLUDE}
)

target_link_libraries(sds 
    PRIVATE ${PAHO_MQTT_LIB}
)

# On macOS, we may need to add include path for Homebrew
if(APPLE)
    target_include_directories(sds PRIVATE /opt/homebrew/include /usr/local/include)
endif()

# Tests
if(SDS_BUILD_TESTS)
    add_executable(test_sds_basic tests/test_basic.c)
    target_link_libraries(test_sds_basic sds ${PAHO_MQTT_LIB})
    target_include_directories(test_sds_basic PRIVATE ${PAHO_MQTT_INCLUDE})
    
    add_executable(test_multi_node tests/test_multi_node.c)
    target_link_libraries(test_multi_node sds ${PAHO_MQTT_LIB})
    target_include_directories(test_multi_node PRIVATE ${PAHO_MQTT_INCLUDE})
    
    add_executable(test_generated tests/test_generated.c)
    target_link_libraries(test_generated sds ${PAHO_MQTT_LIB})
    target_include_directories(test_generated PRIVATE ${PAHO_MQTT_INCLUDE})
    
    add_executable(test_simple_api tests/test_simple_api.c)
    target_link_libraries(test_simple_api sds ${PAHO_MQTT_LIB})
    target_include_directories(test_simple_api PRIVATE ${PAHO_MQTT_INCLUDE})
    
    add_executable(test_liveness tests/test_liveness.c)
    target_link_libraries(test_liveness sds ${PAHO_MQTT_LIB})
    target_include_directories(test_liveness PRIVATE ${PAHO_MQTT_INCLUDE})
    
    add_executable(test_errors tests/test_errors.c)
    target_link_libraries(test_errors sds ${PAHO_MQTT_LIB})
    target_include_directories(test_errors PRIVATE ${PAHO_MQTT_INCLUDE})
    
    # JSON-only test (no MQTT dependency)
    add_executable(test_json tests/test_json.c src/sds_json.c)
    target_include_directories(test_json PRIVATE include)
    target_link_libraries(test_json m)  # Link math library for fabsf
    
    if(APPLE)
        target_include_directories(test_sds_basic PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_multi_node PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_generated PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_simple_api PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_liveness PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_errors PRIVATE /opt/homebrew/include /usr/local/include)
    endif()
endif()

# Examples
if(SDS_BUILD_EXAMPLES)
    add_executable(example_device examples/linux_owner/main.c)
    target_link_libraries(example_device sds ${PAHO_MQTT_LIB})
    target_include_directories(example_device PRIVATE ${PAHO_MQTT_INCLUDE})
    
    if(APPLE)
        target_include_directories(example_device PRIVATE /opt/homebrew/include /usr/local/include)
    endif()
endif()

# Installation
install(TARGETS sds
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include/sds)

