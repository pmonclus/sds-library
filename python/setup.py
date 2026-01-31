"""
Setup script for editable installs and CFFI extension building.
"""
from setuptools import setup

# Don't import from sds directly - use cffi_modules string reference instead
setup(
    cffi_modules=["sds/_build_ffi.py:ffibuilder"],
)
