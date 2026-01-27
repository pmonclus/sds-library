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
    # =========================================================================
    # Unit Tests (Mock Platform - No MQTT broker required)
    # =========================================================================
    
    # SDS library with mock platform for unit testing
    add_library(sds_mock STATIC
        src/sds_core.c
        src/sds_json.c
        tests/mock/sds_platform_mock.c
    )
    target_include_directories(sds_mock 
        PUBLIC include
        PRIVATE tests
    )
    
    # Unit tests using mock platform
    add_executable(test_unit_core tests/test_unit_core.c)
    target_link_libraries(test_unit_core sds_mock m)
    target_include_directories(test_unit_core PRIVATE include tests)
    
    # Reconnection scenario tests
    add_executable(test_reconnection tests/test_reconnection.c)
    target_link_libraries(test_reconnection sds_mock m)
    target_include_directories(test_reconnection PRIVATE include tests)
    
    # Buffer overflow tests
    add_executable(test_buffer_overflow tests/test_buffer_overflow.c)
    target_link_libraries(test_buffer_overflow sds_mock m)
    target_include_directories(test_buffer_overflow PRIVATE include tests)
    
    # Utility function tests
    add_executable(test_utilities tests/test_utilities.c)
    target_link_libraries(test_utilities sds_mock m)
    target_include_directories(test_utilities PRIVATE include tests)
    
    # JSON-only test (no platform dependency)
    add_executable(test_json tests/test_json.c src/sds_json.c)
    target_include_directories(test_json PRIVATE include)
    target_link_libraries(test_json m)
    
    # Concurrent access tests (requires pthreads)
    find_package(Threads)
    if(Threads_FOUND)
        add_executable(test_concurrent tests/test_concurrent.c)
        target_link_libraries(test_concurrent sds_mock m Threads::Threads)
        target_include_directories(test_concurrent PRIVATE include tests)
    endif()
    
    # =========================================================================
    # Fuzz Targets (optional, requires AFL or libFuzzer)
    # =========================================================================
    
    option(SDS_BUILD_FUZZ "Build fuzz targets" OFF)
    
    if(SDS_BUILD_FUZZ)
        # JSON fuzzer (standalone, no mock needed)
        add_executable(fuzz_json tests/fuzz/fuzz_json_parser.c src/sds_json.c)
        target_include_directories(fuzz_json PRIVATE include)
        target_link_libraries(fuzz_json m)
        
        # MQTT message fuzzer (uses mock platform)
        add_executable(fuzz_mqtt tests/fuzz/fuzz_mqtt_message.c)
        target_link_libraries(fuzz_mqtt sds_mock m)
        target_include_directories(fuzz_mqtt PRIVATE include tests)
    endif()
    
    # =========================================================================
    # Integration Tests (Real MQTT broker required)
    # =========================================================================
    
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
    
    # =========================================================================
    # Scale Tests (Real MQTT broker required)
    # =========================================================================
    
    add_executable(test_scale_owner tests/scale/test_scale_owner.c)
    target_link_libraries(test_scale_owner sds ${PAHO_MQTT_LIB})
    target_include_directories(test_scale_owner PRIVATE ${PAHO_MQTT_INCLUDE})
    
    add_executable(test_scale_device tests/scale/test_scale_device.c)
    target_link_libraries(test_scale_device sds ${PAHO_MQTT_LIB})
    target_include_directories(test_scale_device PRIVATE ${PAHO_MQTT_INCLUDE})
    
    if(APPLE)
        target_include_directories(test_sds_basic PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_multi_node PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_generated PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_simple_api PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_liveness PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_errors PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_scale_owner PRIVATE /opt/homebrew/include /usr/local/include)
        target_include_directories(test_scale_device PRIVATE /opt/homebrew/include /usr/local/include)
    endif()
    
    # =========================================================================
    # Sanitizer Builds (Optional)
    # =========================================================================
    
    option(SDS_ENABLE_ASAN "Build with AddressSanitizer" OFF)
    option(SDS_ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
    
    if(SDS_ENABLE_ASAN)
        message(STATUS "AddressSanitizer enabled")
        target_compile_options(sds_mock PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(sds_mock PRIVATE -fsanitize=address)
        target_compile_options(test_unit_core PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(test_unit_core PRIVATE -fsanitize=address)
        target_compile_options(test_json PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(test_json PRIVATE -fsanitize=address)
    endif()
    
    if(SDS_ENABLE_UBSAN)
        message(STATUS "UndefinedBehaviorSanitizer enabled")
        target_compile_options(sds_mock PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(sds_mock PRIVATE -fsanitize=undefined)
        target_compile_options(test_unit_core PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(test_unit_core PRIVATE -fsanitize=undefined)
        target_compile_options(test_json PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(test_json PRIVATE -fsanitize=undefined)
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

