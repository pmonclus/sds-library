"""
Setup script for editable installs and CFFI extension building.
"""
from setuptools import setup

# Import the CFFI builder to register the extension
from sds._build_ffi import ffibuilder

setup(
    cffi_modules=["sds/_build_ffi.py:ffibuilder"],
)
