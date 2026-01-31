"""
CFFI build script for SDS library bindings.

This script is used by setup.py to compile the CFFI extension.
It can also be run directly to build the extension in-place.
"""
import os
import sys
import subprocess
from pathlib import Path

from cffi import FFI

# Find the project root (parent of python/)
# Use resolve() to get absolute paths
PYTHON_DIR = Path(__file__).resolve().parent.parent
PROJECT_ROOT = PYTHON_DIR.parent

# Paths to C sources
INCLUDE_DIR = PROJECT_ROOT / "include"
SRC_DIR = PROJECT_ROOT / "src"
PLATFORM_DIR = PROJECT_ROOT / "platform"

print(f"Building CFFI extension with:")
print(f"  PROJECT_ROOT: {PROJECT_ROOT}")
print(f"  INCLUDE_DIR: {INCLUDE_DIR}")
print(f"  SRC_DIR: {SRC_DIR}")

ffibuilder = FFI()

# Read the C definitions
cdefs_path = Path(__file__).parent / "_cdefs.h"
with open(cdefs_path, "r") as f:
    cdefs = f.read()

ffibuilder.cdef(cdefs)

# Source code to compile - we'll compile the C library directly into the extension
# Include sds_types.h to get the auto-registered table metadata (SensorData, ActuatorData, etc.)
# Include sds_platform.h for log level types and functions
source_code = """
#include "sds.h"
#include "sds_json.h"
#include "sds_error.h"
#include "sds_platform.h"  /* For SdsLogLevel, sds_set_log_level, sds_get_log_level */
#include "sds_types.h"  /* Generated types - auto-registers via constructor */
"""

# Get include directories and source files
include_dirs = [str(INCLUDE_DIR)]
library_dirs = []
sources = []

# For setuptools, we need relative paths from python/ directory
# But for the compiler, we need include paths to be absolute
def make_relative_source(path):
    """Convert absolute path to relative from python/ directory."""
    return os.path.relpath(str(path), str(PYTHON_DIR))

# Add all C source files
for src_file in SRC_DIR.glob("*.c"):
    sources.append(make_relative_source(src_file))

# Add platform implementation (POSIX for macOS/Linux)
platform_posix_c = PLATFORM_DIR / "posix" / "sds_platform_posix.c"
if platform_posix_c.exists():
    sources.append(make_relative_source(platform_posix_c))
    print(f"Using POSIX platform: {platform_posix_c}")
else:
    print(f"Warning: Platform implementation not found at {platform_posix_c}")

# Platform-specific library paths
# The POSIX platform uses Paho MQTT C library (paho-mqtt3c)
libraries = ["paho-mqtt3c"]

if sys.platform == "darwin":
    # macOS: Use Homebrew paths for Paho MQTT
    homebrew_prefix = None
    
    # Try to get Homebrew prefix
    try:
        result = subprocess.run(
            ["brew", "--prefix", "libpaho-mqtt"],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            homebrew_prefix = result.stdout.strip()
    except FileNotFoundError:
        pass
    
    # Fallback to common locations
    if not homebrew_prefix:
        for prefix in ["/opt/homebrew/opt/libpaho-mqtt", "/usr/local/opt/libpaho-mqtt"]:
            if Path(prefix).exists():
                homebrew_prefix = prefix
                break
    
    if homebrew_prefix:
        include_dirs.append(f"{homebrew_prefix}/include")
        library_dirs.append(f"{homebrew_prefix}/lib")
        print(f"Using Homebrew Paho MQTT at: {homebrew_prefix}")
    else:
        print("Warning: Could not find Homebrew Paho MQTT installation")
        print("Install with: brew install libpaho-mqtt")

elif sys.platform == "linux":
    # Linux: Check for pkg-config
    try:
        result = subprocess.run(
            ["pkg-config", "--cflags", "--libs", "paho-mqtt3c"],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            # Parse pkg-config output
            for flag in result.stdout.split():
                if flag.startswith("-I"):
                    include_dirs.append(flag[2:])
                elif flag.startswith("-L"):
                    library_dirs.append(flag[2:])
    except FileNotFoundError:
        pass

# Define the extension module
ffibuilder.set_source(
    "sds._sds_cffi",
    source_code,
    sources=sources,
    include_dirs=include_dirs,
    library_dirs=library_dirs,
    libraries=libraries,
    # Extra compile args for safety
    extra_compile_args=["-Wall", "-Wextra"],
)


def build_inplace():
    """Build the extension in-place for development."""
    ffibuilder.compile(verbose=True)


if __name__ == "__main__":
    build_inplace()
