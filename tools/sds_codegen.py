#!/usr/bin/env python3
"""
SDS Code Generator

Generates C and Python type definitions from .sds schema files.

Usage:
    python sds_codegen.py schema.sds [--output-dir DIR] [--c] [--python]

This produces:
    - sds_types.h  (C structs, serializers, table registry)
    - sds_types.py (Python dataclasses for use with sds library)
"""

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import List, Optional, Dict


# ============== Schema Data Structures ==============


@dataclass
class Field:
    """A field within a section."""
    name: str
    type: str  # uint8, uint16, uint32, int8, int16, int32, float, bool, string
    default: Optional[str] = None
    string_len: int = 32  # Default string length


@dataclass 
class Section:
    """A section (config, state, or status) within a table."""
    name: str  # "config", "state", or "status"
    fields: List[Field] = field(default_factory=list)


@dataclass
class Table:
    """A table definition."""
    name: str
    sync_interval: int = 1000
    liveness: int = 3000
    config: Optional[Section] = None
    state: Optional[Section] = None
    status: Optional[Section] = None


@dataclass
class Schema:
    """The complete schema."""
    version: str = "1.0.0"
    max_nodes: int = 16
    tables: List[Table] = field(default_factory=list)


# ============== Parser ==============


class SchemaParser:
    """Parser for .sds schema files."""
    
    # Type mapping from schema to C types
    C_TYPES = {
        "uint8": "uint8_t",
        "uint16": "uint16_t",
        "uint32": "uint32_t",
        "int8": "int8_t",
        "int16": "int16_t",
        "int32": "int32_t",
        "float": "float",
        "bool": "bool",
        "string": "char",
    }
    
    def __init__(self, content: str):
        self.content = content
        self.pos = 0
        self.line = 1
    
    def parse(self) -> Schema:
        """Parse the schema file."""
        schema = Schema()
        
        while self.pos < len(self.content):
            self._skip_whitespace_and_comments()
            if self.pos >= len(self.content):
                break
            
            # Check for directives
            if self._peek() == '@':
                key, value = self._parse_directive()
                if key == "version":
                    schema.version = value.strip('"\'')
                elif key == "max_nodes":
                    schema.max_nodes = int(value)
            elif self._match_keyword("table"):
                table = self._parse_table()
                schema.tables.append(table)
            else:
                self._error(f"Unexpected token: {self._peek()}")
        
        return schema
    
    def _skip_whitespace_and_comments(self):
        """Skip whitespace and comments."""
        while self.pos < len(self.content):
            c = self.content[self.pos]
            if c in ' \t\r':
                self.pos += 1
            elif c == '\n':
                self.pos += 1
                self.line += 1
            elif c == '/' and self.pos + 1 < len(self.content):
                if self.content[self.pos + 1] == '/':
                    # Line comment
                    while self.pos < len(self.content) and self.content[self.pos] != '\n':
                        self.pos += 1
                elif self.content[self.pos + 1] == '*':
                    # Block comment
                    self.pos += 2
                    while self.pos + 1 < len(self.content):
                        if self.content[self.pos] == '*' and self.content[self.pos + 1] == '/':
                            self.pos += 2
                            break
                        if self.content[self.pos] == '\n':
                            self.line += 1
                        self.pos += 1
                else:
                    break
            else:
                break
    
    def _peek(self) -> str:
        """Peek at current character."""
        if self.pos < len(self.content):
            return self.content[self.pos]
        return ''
    
    def _match_keyword(self, keyword: str) -> bool:
        """Match a keyword."""
        self._skip_whitespace_and_comments()
        if self.content[self.pos:self.pos + len(keyword)] == keyword:
            # Check it's not part of a larger identifier
            next_pos = self.pos + len(keyword)
            if next_pos >= len(self.content) or not self.content[next_pos].isalnum():
                self.pos = next_pos
                return True
        return False
    
    def _parse_identifier(self) -> str:
        """Parse an identifier."""
        self._skip_whitespace_and_comments()
        start = self.pos
        while self.pos < len(self.content) and (self.content[self.pos].isalnum() or self.content[self.pos] == '_'):
            self.pos += 1
        if self.pos == start:
            self._error("Expected identifier")
        return self.content[start:self.pos]
    
    def _parse_value(self) -> str:
        """Parse a value (number, string, or identifier)."""
        self._skip_whitespace_and_comments()
        start = self.pos
        
        if self._peek() == '"':
            # String value
            self.pos += 1
            while self.pos < len(self.content) and self.content[self.pos] != '"':
                self.pos += 1
            self.pos += 1
            return self.content[start:self.pos]
        else:
            # Number or identifier
            while self.pos < len(self.content) and (self.content[self.pos].isalnum() or self.content[self.pos] in '._-'):
                self.pos += 1
            return self.content[start:self.pos]
    
    def _expect(self, char: str):
        """Expect a specific character."""
        self._skip_whitespace_and_comments()
        if self._peek() != char:
            self._error(f"Expected '{char}', got '{self._peek()}'")
        self.pos += 1
    
    def _parse_directive(self) -> tuple:
        """Parse a directive like @version = "1.0.0"."""
        self._expect('@')
        key = self._parse_identifier()
        self._expect('=')
        value = self._parse_value()
        return key, value
    
    def _parse_table(self) -> Table:
        """Parse a table definition."""
        name = self._parse_identifier()
        table = Table(name=name)
        
        self._expect('{')
        
        while True:
            self._skip_whitespace_and_comments()
            if self._peek() == '}':
                self.pos += 1
                break
            
            if self._peek() == '@':
                key, value = self._parse_directive()
                if key == "sync_interval":
                    table.sync_interval = int(value)
                elif key == "liveness":
                    table.liveness = int(value)
            elif self._match_keyword("config"):
                table.config = self._parse_section("config")
            elif self._match_keyword("state"):
                table.state = self._parse_section("state")
            elif self._match_keyword("status"):
                table.status = self._parse_section("status")
            else:
                self._error(f"Unexpected token in table: {self._peek()}")
        
        return table
    
    def _parse_section(self, name: str) -> Section:
        """Parse a section (config, state, or status)."""
        section = Section(name=name)
        
        self._expect('{')
        
        while True:
            self._skip_whitespace_and_comments()
            if self._peek() == '}':
                self.pos += 1
                break
            
            field = self._parse_field()
            section.fields.append(field)
        
        return section
    
    def _parse_field(self) -> Field:
        """Parse a field definition like 'uint8 command = 0;'."""
        type_name = self._parse_identifier()
        
        # Handle string with length: string[32]
        string_len = 32
        self._skip_whitespace_and_comments()
        if self._peek() == '[':
            self.pos += 1
            len_str = ""
            while self._peek().isdigit():
                len_str += self._peek()
                self.pos += 1
            string_len = int(len_str) if len_str else 32
            self._expect(']')
        
        field_name = self._parse_identifier()
        
        default = None
        self._skip_whitespace_and_comments()
        if self._peek() == '=':
            self.pos += 1
            default = self._parse_value()
        
        self._expect(';')
        
        return Field(name=field_name, type=type_name, default=default, string_len=string_len)
    
    def _error(self, msg: str):
        """Raise a parse error."""
        raise ValueError(f"Parse error at line {self.line}: {msg}")


# ============== C Code Generator ==============


class CGenerator:
    """Generate C header file from schema."""
    
    C_TYPES = {
        "uint8": "uint8_t",
        "uint16": "uint16_t", 
        "uint32": "uint32_t",
        "int8": "int8_t",
        "int16": "int16_t",
        "int32": "int32_t",
        "float": "float",
        "bool": "bool",
        "string": "char",
    }
    
    def generate(self, schema: Schema) -> str:
        """Generate the C header file content."""
        lines = []
        
        # Header
        lines.append("/*")
        lines.append(" * sds_types.h - Auto-generated from schema.sds")
        lines.append(" * ")
        lines.append(" * DO NOT EDIT - This file is generated by sds-codegen")
        lines.append(f" * Schema version: {schema.version}")
        lines.append(" */")
        lines.append("")
        lines.append("#ifndef SDS_TYPES_H")
        lines.append("#define SDS_TYPES_H")
        lines.append("")
        lines.append(f'#define SDS_SCHEMA_VERSION "{schema.version}"')
        lines.append("")
        lines.append("#include <stdint.h>")
        lines.append("#include <stdbool.h>")
        lines.append("#include <stddef.h>")
        lines.append('#include "sds.h"')
        lines.append('#include "sds_json.h"')
        lines.append("")
        lines.append(f"#define SDS_GENERATED_MAX_NODES {schema.max_nodes}")
        lines.append("")
        
        # Generate each table
        for table in schema.tables:
            lines.extend(self._generate_table(table))
            lines.append("")
        
        # Max section size
        lines.extend(self._generate_max_section_size(schema))
        lines.append("")
        
        # Table registry
        lines.extend(self._generate_registry(schema))
        lines.append("")
        
        lines.append("#endif /* SDS_TYPES_H */")
        
        return "\n".join(lines)
    
    def _to_snake_case(self, name: str) -> str:
        """Convert CamelCase to snake_case."""
        return re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower()
    
    def _to_macro_case(self, name: str) -> str:
        """Convert CamelCase to MACRO_CASE."""
        return self._to_snake_case(name).upper()
    
    def _generate_table(self, table: Table) -> List[str]:
        """Generate code for a single table."""
        lines = []
        name = table.name
        snake = self._to_snake_case(name)
        macro = self._to_macro_case(name)
        
        lines.append(f"/* ============== Table: {name} ============== */")
        lines.append("")
        lines.append(f"#define SDS_{macro}_SYNC_INTERVAL_MS {table.sync_interval}")
        lines.append(f"#define SDS_{macro}_LIVENESS_INTERVAL_MS {table.liveness}")
        lines.append("")
        
        # Generate sections
        if table.config:
            lines.extend(self._generate_section_struct(name, "Config", table.config))
        if table.state:
            lines.extend(self._generate_section_struct(name, "State", table.state))
        if table.status:
            lines.extend(self._generate_section_struct(name, "Status", table.status))
        
        # Device table
        lines.append(f"/* Device Table (for SDS_ROLE_DEVICE) */")
        lines.append(f"typedef struct {{")
        if table.config:
            lines.append(f"    {name}Config config;")
        if table.state:
            lines.append(f"    {name}State state;")
        if table.status:
            lines.append(f"    {name}Status status;")
        lines.append(f"}} {name}Table;")
        lines.append("")
        
        # Status slot
        if table.status:
            lines.append(f"/* Status slot for per-device tracking at Owner */")
            lines.append(f"typedef struct {{")
            lines.append(f"    char node_id[SDS_MAX_NODE_ID_LEN];")
            lines.append(f"    bool valid;")
            lines.append(f"    bool online;           /* false if LWT received or graceful disconnect */")
            lines.append(f"    uint32_t last_seen_ms; /* Timestamp of last received status */")
            lines.append(f"    {name}Status status;")
            lines.append(f"}} {name}StatusSlot;")
            lines.append("")
        
        # Owner table
        lines.append(f"/* Owner Table (for SDS_ROLE_OWNER) */")
        lines.append(f"typedef struct {{")
        if table.config:
            lines.append(f"    {name}Config config;")
        if table.state:
            lines.append(f"    {name}State state;  /* Merged from all devices */")
        if table.status:
            lines.append(f"    {name}StatusSlot status_slots[SDS_GENERATED_MAX_NODES];")
            lines.append(f"    uint8_t status_count;")
        lines.append(f"}} {name}OwnerTable;")
        lines.append("")
        
        # Serializers
        if table.config:
            lines.extend(self._generate_serializer(name, "config", table.config))
        if table.state:
            lines.extend(self._generate_serializer(name, "state", table.state))
        if table.status:
            lines.extend(self._generate_serializer(name, "status", table.status))
        
        # Deserializers
        if table.config:
            lines.extend(self._generate_deserializer(name, "config", table.config))
        if table.state:
            lines.extend(self._generate_deserializer(name, "state", table.state))
        if table.status:
            lines.extend(self._generate_deserializer(name, "status", table.status))
        
        return lines
    
    def _generate_section_struct(self, table_name: str, section_name: str, section: Section) -> List[str]:
        """Generate a section struct."""
        lines = []
        comments = {
            "Config": "/* Config section (Owner -> Devices) */",
            "State": "/* State section (All -> Owner, LWW) */",
            "Status": "/* Status section (Device -> Owner) */",
        }
        
        lines.append(comments.get(section_name, f"/* {section_name} section */"))
        lines.append(f"typedef struct {{")
        
        for f in section.fields:
            c_type = self.C_TYPES.get(f.type, f.type)
            if f.type == "string":
                lines.append(f"    {c_type} {f.name}[{f.string_len}];")
            else:
                lines.append(f"    {c_type} {f.name};")
        
        lines.append(f"}} {table_name}{section_name};")
        lines.append("")
        return lines
    
    def _generate_serializer(self, table_name: str, section_name: str, section: Section) -> List[str]:
        """Generate JSON serializer function."""
        lines = []
        snake = self._to_snake_case(table_name)
        type_name = f"{table_name}{section_name.capitalize()}"
        var = section_name[:2]  # cfg, st, etc.
        
        lines.append(f"static void {snake}_serialize_{section_name}(void* section, SdsJsonWriter* w) {{")
        lines.append(f"    {type_name}* {var} = ({type_name}*)section;")
        
        for f in section.fields:
            if f.type == "float":
                lines.append(f'    sds_json_add_float(w, "{f.name}", {var}->{f.name});')
            elif f.type == "bool":
                lines.append(f'    sds_json_add_bool(w, "{f.name}", {var}->{f.name});')
            elif f.type == "string":
                lines.append(f'    sds_json_add_string(w, "{f.name}", {var}->{f.name});')
            elif f.type.startswith("int"):
                lines.append(f'    sds_json_add_int(w, "{f.name}", {var}->{f.name});')
            else:
                lines.append(f'    sds_json_add_uint(w, "{f.name}", {var}->{f.name});')
        
        lines.append("}")
        lines.append("")
        return lines
    
    def _generate_deserializer(self, table_name: str, section_name: str, section: Section) -> List[str]:
        """Generate JSON deserializer function."""
        lines = []
        snake = self._to_snake_case(table_name)
        type_name = f"{table_name}{section_name.capitalize()}"
        var = section_name[:2]
        
        lines.append(f"static void {snake}_deserialize_{section_name}(void* section, SdsJsonReader* r) {{")
        lines.append(f"    {type_name}* {var} = ({type_name}*)section;")
        
        for f in section.fields:
            if f.type == "uint8":
                lines.append(f'    sds_json_get_uint8_field(r, "{f.name}", &{var}->{f.name});')
            elif f.type == "float":
                lines.append(f'    sds_json_get_float_field(r, "{f.name}", &{var}->{f.name});')
            elif f.type == "bool":
                lines.append(f'    sds_json_get_bool_field(r, "{f.name}", &{var}->{f.name});')
            elif f.type == "string":
                lines.append(f'    sds_json_get_string_field(r, "{f.name}", {var}->{f.name}, sizeof({var}->{f.name}));')
            elif f.type.startswith("int"):
                lines.append(f'    sds_json_get_int_field(r, "{f.name}", (int32_t*)&{var}->{f.name});')
            elif f.type in ("uint16", "uint32"):
                lines.append(f'    {{ uint32_t tmp; if (sds_json_get_uint_field(r, "{f.name}", &tmp)) {var}->{f.name} = tmp; }}')
            else:
                lines.append(f'    sds_json_get_uint_field(r, "{f.name}", &{var}->{f.name});')
        
        lines.append("}")
        lines.append("")
        return lines
    
    def _generate_max_section_size(self, schema: Schema) -> List[str]:
        """Generate max section size macro."""
        lines = []
        lines.append("/* ============== Max Section Size (for shadow buffers) ============== */")
        lines.append("")
        lines.append("/* Helper macros for compile-time max calculation */")
        lines.append("#define _SDS_MAX2(a, b) ((a) > (b) ? (a) : (b))")
        lines.append("#define _SDS_MAX3(a, b, c) _SDS_MAX2(_SDS_MAX2(a, b), c)")
        lines.append("")
        
        # Build max expression
        sizes = []
        for table in schema.tables:
            if table.config:
                sizes.append(f"sizeof({table.name}Config)")
            if table.state:
                sizes.append(f"sizeof({table.name}State)")
            if table.status:
                sizes.append(f"sizeof({table.name}Status)")
        
        if sizes:
            expr = sizes[0]
            for s in sizes[1:]:
                expr = f"_SDS_MAX2({expr}, {s})"
            lines.append(f"/* Auto-calculated maximum section size across all tables */")
            lines.append(f"#define SDS_GENERATED_MAX_SECTION_SIZE ({expr})")
        
        return lines
    
    def _generate_registry(self, schema: Schema) -> List[str]:
        """Generate the table registry."""
        lines = []
        lines.append("/* ============== Table Registry ============== */")
        lines.append("")
        lines.append(f"#define SDS_TABLE_REGISTRY_COUNT {len(schema.tables)}")
        lines.append("")
        lines.append("static const SdsTableMeta SDS_TABLE_REGISTRY[] = {")
        
        for table in schema.tables:
            lines.extend(self._generate_registry_entry(table))
        
        lines.append("};")
        lines.append("")
        lines.append("/* Auto-register tables with SDS core (runs before main) */")
        lines.append("#if defined(__GNUC__) || defined(__clang__)")
        lines.append("__attribute__((constructor))")
        lines.append("#endif")
        lines.append("static void _sds_auto_register_types(void) {")
        lines.append("    sds_set_table_registry(SDS_TABLE_REGISTRY, SDS_TABLE_REGISTRY_COUNT);")
        lines.append("    sds_set_schema_version(SDS_SCHEMA_VERSION);")
        lines.append("}")
        
        return lines
    
    def _generate_registry_entry(self, table: Table) -> List[str]:
        """Generate a registry entry for a table."""
        lines = []
        name = table.name
        snake = self._to_snake_case(name)
        macro = self._to_macro_case(name)
        
        lines.append(f"    /* {name} */")
        lines.append(f"    {{")
        lines.append(f'        .table_type = "{name}",')
        lines.append(f"        .sync_interval_ms = SDS_{macro}_SYNC_INTERVAL_MS,")
        lines.append(f"        .liveness_interval_ms = SDS_{macro}_LIVENESS_INTERVAL_MS,")
        lines.append(f"        .device_table_size = sizeof({name}Table),")
        lines.append(f"        .owner_table_size = sizeof({name}OwnerTable),")
        
        if table.config:
            lines.append(f"        .dev_config_offset = offsetof({name}Table, config),")
            lines.append(f"        .dev_config_size = sizeof({name}Config),")
        else:
            lines.append(f"        .dev_config_offset = 0,")
            lines.append(f"        .dev_config_size = 0,")
        
        if table.state:
            lines.append(f"        .dev_state_offset = offsetof({name}Table, state),")
            lines.append(f"        .dev_state_size = sizeof({name}State),")
        else:
            lines.append(f"        .dev_state_offset = 0,")
            lines.append(f"        .dev_state_size = 0,")
        
        if table.status:
            lines.append(f"        .dev_status_offset = offsetof({name}Table, status),")
            lines.append(f"        .dev_status_size = sizeof({name}Status),")
        else:
            lines.append(f"        .dev_status_offset = 0,")
            lines.append(f"        .dev_status_size = 0,")
        
        if table.config:
            lines.append(f"        .own_config_offset = offsetof({name}OwnerTable, config),")
            lines.append(f"        .own_config_size = sizeof({name}Config),")
        else:
            lines.append(f"        .own_config_offset = 0,")
            lines.append(f"        .own_config_size = 0,")
        
        if table.state:
            lines.append(f"        .own_state_offset = offsetof({name}OwnerTable, state),")
            lines.append(f"        .own_state_size = sizeof({name}State),")
        else:
            lines.append(f"        .own_state_offset = 0,")
            lines.append(f"        .own_state_size = 0,")
        
        if table.status:
            lines.append(f"        .own_status_slots_offset = offsetof({name}OwnerTable, status_slots),")
            lines.append(f"        .own_status_slot_size = sizeof({name}StatusSlot),")
            lines.append(f"        .own_status_count_offset = offsetof({name}OwnerTable, status_count),")
            lines.append(f"        .slot_valid_offset = offsetof({name}StatusSlot, valid),")
            lines.append(f"        .slot_online_offset = offsetof({name}StatusSlot, online),")
            lines.append(f"        .slot_last_seen_offset = offsetof({name}StatusSlot, last_seen_ms),")
            lines.append(f"        .slot_status_offset = offsetof({name}StatusSlot, status),")
            lines.append(f"        .own_max_status_slots = SDS_GENERATED_MAX_NODES,")
        else:
            lines.append(f"        .own_status_slots_offset = 0,")
            lines.append(f"        .own_status_slot_size = 0,")
            lines.append(f"        .own_status_count_offset = 0,")
            lines.append(f"        .slot_valid_offset = 0,")
            lines.append(f"        .slot_online_offset = 0,")
            lines.append(f"        .slot_last_seen_offset = 0,")
            lines.append(f"        .slot_status_offset = 0,")
            lines.append(f"        .own_max_status_slots = 0,")
        
        lines.append(f"        .serialize_config = {snake}_serialize_config," if table.config else "        .serialize_config = NULL,")
        lines.append(f"        .serialize_state = {snake}_serialize_state," if table.state else "        .serialize_state = NULL,")
        lines.append(f"        .serialize_status = {snake}_serialize_status," if table.status else "        .serialize_status = NULL,")
        lines.append(f"        .deserialize_config = {snake}_deserialize_config," if table.config else "        .deserialize_config = NULL,")
        lines.append(f"        .deserialize_state = {snake}_deserialize_state," if table.state else "        .deserialize_state = NULL,")
        lines.append(f"        .deserialize_status = {snake}_deserialize_status," if table.status else "        .deserialize_status = NULL,")
        lines.append(f"    }},")
        
        return lines


# ============== Python Code Generator ==============


class PythonGenerator:
    """Generate Python type definitions from schema."""
    
    # Type mapping from schema to Python Field parameters
    PYTHON_FIELDS = {
        "uint8": "uint8=True",
        "uint16": "uint16=True",
        "uint32": "uint32=True",
        "int8": "int8=True",
        "int16": "int16=True",
        "int32": "int32=True",
        "float": "float32=True",
        "bool": "",  # bool is the default
        "string": "string_len={len}",
    }
    
    def generate(self, schema: Schema) -> str:
        """Generate the Python module content."""
        lines = []
        
        # Header
        lines.append('"""')
        lines.append("sds_types.py - Auto-generated from schema.sds")
        lines.append("")
        lines.append("DO NOT EDIT - This file is generated by sds-codegen")
        lines.append(f"Schema version: {schema.version}")
        lines.append("")
        lines.append("Usage:")
        lines.append("    from sds_types import SensorData")
        lines.append("")
        lines.append('    table = node.register_table("SensorData", Role.DEVICE, schema=SensorData)')
        lines.append("    table.state.temperature = 23.5")
        lines.append('"""')
        lines.append("")
        lines.append("from dataclasses import dataclass")
        lines.append("from sds import Field")
        lines.append("")
        lines.append(f'SCHEMA_VERSION = "{schema.version}"')
        lines.append("")
        
        # Generate each table
        for table in schema.tables:
            lines.extend(self._generate_table(table))
            lines.append("")
        
        # Generate __all__
        table_names = [t.name for t in schema.tables]
        lines.append("# All exported types")
        lines.append(f"__all__ = {table_names + ['SCHEMA_VERSION']}")
        
        return "\n".join(lines)
    
    def _generate_table(self, table: Table) -> List[str]:
        """Generate code for a single table."""
        lines = []
        name = table.name
        
        lines.append(f"# ============== {name} ==============")
        lines.append("")
        
        # Generate section dataclasses
        if table.config:
            lines.extend(self._generate_section(name, "Config", table.config))
        if table.state:
            lines.extend(self._generate_section(name, "State", table.state))
        if table.status:
            lines.extend(self._generate_section(name, "Status", table.status))
        
        # Generate bundle class
        lines.append("")
        lines.append(f"class {name}:")
        lines.append(f'    """')
        lines.append(f"    Schema bundle for {name} table.")
        lines.append(f"    ")
        lines.append(f"    Usage:")
        lines.append(f'        table = node.register_table("{name}", Role.DEVICE, schema={name})')
        lines.append(f'    """')
        if table.config:
            lines.append(f"    Config = {name}Config")
        if table.state:
            lines.append(f"    State = {name}State")
        if table.status:
            lines.append(f"    Status = {name}Status")
        lines.append("")
        
        return lines
    
    def _generate_section(self, table_name: str, section_name: str, section: Section) -> List[str]:
        """Generate a section dataclass."""
        lines = []
        
        lines.append("")
        lines.append("@dataclass")
        lines.append(f"class {table_name}{section_name}:")
        lines.append(f'    """{section_name} section of {table_name} table."""')
        
        for f in section.fields:
            field_args = self._get_field_args(f)
            default = self._get_default(f)
            if field_args:
                lines.append(f"    {f.name}: {self._get_python_type(f)} = Field({field_args}, default={default})")
            else:
                lines.append(f"    {f.name}: {self._get_python_type(f)} = Field(default={default})")
        
        return lines
    
    def _get_python_type(self, f: Field) -> str:
        """Get Python type annotation for a field."""
        if f.type in ("uint8", "uint16", "uint32", "int8", "int16", "int32"):
            return "int"
        elif f.type == "float":
            return "float"
        elif f.type == "bool":
            return "bool"
        elif f.type == "string":
            return "str"
        return "any"
    
    def _get_field_args(self, f: Field) -> str:
        """Get Field() arguments for a field."""
        mapping = {
            "uint8": "uint8=True",
            "uint16": "uint16=True",
            "uint32": "uint32=True",
            "int8": "int8=True",
            "int16": "int16=True",
            "int32": "int32=True",
            "float": "float32=True",
            "bool": "",
            "string": f"string_len={f.string_len}",
        }
        return mapping.get(f.type, "")
    
    def _get_default(self, f: Field) -> str:
        """Get default value for a field."""
        if f.default is not None:
            return f.default
        
        # Provide sensible defaults
        if f.type in ("uint8", "uint16", "uint32", "int8", "int16", "int32"):
            return "0"
        elif f.type == "float":
            return "0.0"
        elif f.type == "bool":
            return "False"
        elif f.type == "string":
            return '""'
        return "None"


# ============== Main ==============


def main():
    parser = argparse.ArgumentParser(
        description="Generate C and Python type definitions from SDS schema"
    )
    parser.add_argument("schema", help="Path to .sds schema file")
    parser.add_argument("--output-dir", "-o", default=".", help="Output directory")
    parser.add_argument("--c", action="store_true", help="Generate C header (sds_types.h)")
    parser.add_argument("--python", action="store_true", help="Generate Python module (sds_types.py)")
    parser.add_argument("--all", action="store_true", help="Generate both C and Python (default)")
    
    args = parser.parse_args()
    
    # Default to generating both if neither specified
    if not args.c and not args.python:
        args.c = True
        args.python = True
    
    # Read schema
    schema_path = Path(args.schema)
    if not schema_path.exists():
        print(f"Error: Schema file not found: {schema_path}", file=sys.stderr)
        return 1
    
    content = schema_path.read_text()
    
    # Parse
    try:
        parser_obj = SchemaParser(content)
        schema = parser_obj.parse()
    except ValueError as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    
    print(f"Parsed schema: {schema.version}")
    print(f"  Tables: {[t.name for t in schema.tables]}")
    
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # Generate C
    if args.c:
        c_gen = CGenerator()
        c_content = c_gen.generate(schema)
        c_path = output_dir / "sds_types.h"
        c_path.write_text(c_content)
        print(f"Generated: {c_path}")
    
    # Generate Python
    if args.python:
        py_gen = PythonGenerator()
        py_content = py_gen.generate(schema)
        py_path = output_dir / "sds_types.py"
        py_path.write_text(py_content)
        print(f"Generated: {py_path}")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
