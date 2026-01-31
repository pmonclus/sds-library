#!/usr/bin/env python3
"""
SDS Code Generator CLI

Usage:
    sds-codegen schema.sds --c --python
    sds-codegen schema.sds --c -o ./src/
    sds-codegen schema.sds  # Generates both C and Python
"""

import argparse
import sys
from pathlib import Path

from .parser import parse_file, ParseError
from .c_generator import generate_header_file
from .python_generator import generate_python_file


def main():
    parser = argparse.ArgumentParser(
        description='Generate C and Python types from SDS schema',
        prog='sds-codegen'
    )
    parser.add_argument(
        'schema',
        type=Path,
        help='Path to .sds schema file'
    )
    parser.add_argument(
        '-o', '--output-dir',
        type=Path,
        default=Path('.'),
        help='Output directory (default: current directory)'
    )
    parser.add_argument(
        '--c',
        action='store_true',
        help='Generate C header (sds_types.h)'
    )
    parser.add_argument(
        '--python',
        action='store_true',
        help='Generate Python module (sds_types.py)'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Print verbose output'
    )
    
    args = parser.parse_args()
    
    # Default to generating both if neither specified
    if not args.c and not args.python:
        args.c = True
        args.python = True
    
    if not args.schema.exists():
        print(f"Error: Schema file not found: {args.schema}", file=sys.stderr)
        return 1
    
    try:
        if args.verbose:
            print(f"Parsing {args.schema}...")
        
        schema = parse_file(str(args.schema))
        
        if args.verbose:
            print(f"  Version: {schema.version}")
            print(f"  Tables: {', '.join(schema.tables.keys())}")
            for name, table in schema.tables.items():
                print(f"    {name}:")
                print(f"      config: {len(table.config_fields)} fields")
                print(f"      state: {len(table.state_fields)} fields")
                print(f"      status: {len(table.status_fields)} fields")
                print(f"      sync_interval: {table.sync_interval_ms}ms")
        
        # Ensure output directory exists
        args.output_dir.mkdir(parents=True, exist_ok=True)
        
        # Generate C
        if args.c:
            c_path = args.output_dir / "sds_types.h"
            if args.verbose:
                print(f"Generating {c_path}...")
            generate_header_file(schema, str(c_path))
            print(f"Generated: {c_path}")
        
        # Generate Python
        if args.python:
            py_path = args.output_dir / "sds_types.py"
            if args.verbose:
                print(f"Generating {py_path}...")
            generate_python_file(schema, str(py_path))
            print(f"Generated: {py_path}")
        
        return 0
        
    except ParseError as e:
        print(f"Parse error: {e}", file=sys.stderr)
        return 1
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        if args.verbose:
            import traceback
            traceback.print_exc()
        return 1


if __name__ == '__main__':
    sys.exit(main())
