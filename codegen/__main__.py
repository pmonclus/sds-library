#!/usr/bin/env python3
"""
Enable running the codegen module directly:
    python -m codegen schema.sds --c --python
"""
from .cli import main
import sys

if __name__ == '__main__':
    sys.exit(main())
