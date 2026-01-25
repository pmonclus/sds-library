#!/usr/bin/env python3
"""
SDS Code Generator CLI

Usage:
    python -m codegen.cli schema.sds -o sds_types.h
    python -m codegen.cli schema.sds --output include/sds_types.h
"""

import argparse
import sys
from pathlib import Path

from .parser import parse_file, ParseError
from .c_generator import generate_header_file


def main():
    parser = argparse.ArgumentParser(
        description='Generate C types from SDS schema',
        prog='sds-codegen'
    )
    parser.add_argument(
        'schema',
        type=Path,
        help='Path to .sds schema file'
    )
    parser.add_argument(
        '-o', '--output',
        type=Path,
        default=Path('sds_types.h'),
        help='Output header file path (default: sds_types.h)'
    )
    parser.add_argument(
        '-v', '--verbose',
        action='store_true',
        help='Print verbose output'
    )
    
    args = parser.parse_args()
    
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
        
        if args.verbose:
            print(f"Generating {args.output}...")
        
        generate_header_file(schema, str(args.output))
        
        print(f"Generated: {args.output}")
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

