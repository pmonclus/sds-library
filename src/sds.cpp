/*
 * sds.cpp - SDS Library Build File for Arduino/PlatformIO
 * 
 * This file includes the C source files so they compile correctly
 * in an Arduino/C++ environment.
 * 
 * Note: For POSIX builds, use CMake which compiles .c files directly.
 */

#ifdef ARDUINO

/* Include the C implementation files */
extern "C" {
    #include "sds_core.c"
    #include "sds_json.c"
}

/* Include the ESP32 platform implementation */
#include "../platform/esp32/sds_platform_esp32.cpp"

#endif /* ARDUINO */

