"""
SDS Schema Parser

Parses .sds schema files into an intermediate representation (IR)
that can be used by code generators.

Grammar:
    schema          = version? table*
    version         = '@version' '=' STRING
    
    table           = 'table' IDENT '{' table_body '}'
    table_body      = annotation* (config_section | state_section | status_section)*
    
    config_section  = 'config' '{' field* '}'
    state_section   = 'state' '{' field* '}'
    status_section  = 'status' '{' field* '}'
    
    annotation      = '@' IDENT '=' VALUE
    field           = TYPE IDENT ('=' DEFAULT)? ';'
    TYPE            = BASE_TYPE ('[' NUMBER ']')?
    BASE_TYPE       = 'bool' | 'uint8' | 'int8' | 'uint16' | 'int16' 
                    | 'uint32' | 'int32' | 'float' | 'string'
"""

import re
from dataclasses import dataclass, field as dataclass_field
from typing import Dict, List, Optional, Any
from enum import Enum


class SectionType(Enum):
    CONFIG = "config"
    STATE = "state"
    STATUS = "status"


@dataclass
class Field:
    """A single field within a section."""
    name: str
    type: str                           # Base type (e.g., "uint8", "float")
    array_size: Optional[int] = None    # None = scalar, N = fixed array
    default: Optional[Any] = None
    section: SectionType = SectionType.STATE
    comment: Optional[str] = None


@dataclass
class Table:
    """A table with config/state/status sections."""
    name: str
    sync_interval_ms: int = 1000
    config_fields: List[Field] = dataclass_field(default_factory=list)
    state_fields: List[Field] = dataclass_field(default_factory=list)
    status_fields: List[Field] = dataclass_field(default_factory=list)


@dataclass
class Schema:
    """Complete schema with version and tables."""
    version: str = "1.0.0"
    tables: Dict[str, Table] = dataclass_field(default_factory=dict)


class ParseError(Exception):
    """Error during parsing with line information."""
    def __init__(self, message: str, line: int, col: int = 0):
        self.line = line
        self.col = col
        super().__init__(f"Line {line}: {message}")


class Tokenizer:
    """Simple tokenizer for .sds files."""
    
    TOKEN_PATTERNS = [
        ('COMMENT', r'//[^\n]*'),
        ('WHITESPACE', r'\s+'),
        ('ANNOTATION', r'@[a-zA-Z_][a-zA-Z0-9_]*'),
        ('FLOAT', r'-?\d+\.\d+'),
        ('NUMBER', r'-?\d+'),
        ('STRING', r'"[^"]*"'),
        ('IDENT', r'[a-zA-Z_][a-zA-Z0-9_]*'),
        ('LBRACE', r'\{'),
        ('RBRACE', r'\}'),
        ('LBRACKET', r'\['),
        ('RBRACKET', r'\]'),
        ('EQUALS', r'='),
        ('SEMICOLON', r';'),
        ('COMMA', r','),
    ]
    
    KEYWORDS = {
        'table',
        'config', 'state', 'status',
        'bool', 'uint8', 'int8', 'uint16', 'int16', 'uint32', 'int32', 'float', 'string',
    }
    
    def __init__(self, text: str):
        self.text = text
        self.tokens = []
        self._tokenize()
    
    def _tokenize(self):
        combined = '|'.join(f'(?P<{name}>{pattern})' for name, pattern in self.TOKEN_PATTERNS)
        regex = re.compile(combined)
        
        for match in regex.finditer(self.text):
            kind = match.lastgroup
            value = match.group()
            start = match.start()
            
            # Calculate line and column
            line = self.text.count('\n', 0, start) + 1
            line_start = self.text.rfind('\n', 0, start) + 1
            col = start - line_start + 1
            
            if kind == 'COMMENT' or kind == 'WHITESPACE':
                continue
            
            if kind == 'IDENT' and value in self.KEYWORDS:
                kind = 'KEYWORD'
            
            self.tokens.append((kind, value, line, col))
        
        self.tokens.append(('EOF', '', 0, 0))
    
    def __iter__(self):
        return iter(self.tokens)


class Parser:
    """Recursive descent parser for .sds schema files."""
    
    TYPES = {'bool', 'uint8', 'int8', 'uint16', 'int16', 'uint32', 'int32', 'float', 'string'}
    
    def __init__(self, tokens: List[tuple]):
        self.tokens = tokens
        self.pos = 0
        self.current_section = SectionType.STATE
    
    def current(self) -> tuple:
        return self.tokens[self.pos] if self.pos < len(self.tokens) else ('EOF', '', 0, 0)
    
    def peek(self, offset: int = 0) -> tuple:
        idx = self.pos + offset
        return self.tokens[idx] if idx < len(self.tokens) else ('EOF', '', 0, 0)
    
    def advance(self) -> tuple:
        token = self.current()
        self.pos += 1
        return token
    
    def expect(self, kind: str, value: str = None) -> tuple:
        token = self.current()
        if token[0] != kind:
            raise ParseError(f"Expected {kind}, got {token[0]} '{token[1]}'", token[2], token[3])
        if value is not None and token[1] != value:
            raise ParseError(f"Expected '{value}', got '{token[1]}'", token[2], token[3])
        return self.advance()
    
    def parse(self) -> Schema:
        schema = Schema()
        
        # Check for version annotation at the start
        if self.current()[0] == 'ANNOTATION' and self.current()[1] == '@version':
            _, version = self._parse_annotation()
            schema.version = str(version)
        
        while self.current()[0] != 'EOF':
            token = self.current()
            
            if token[0] == 'KEYWORD' and token[1] == 'table':
                table = self._parse_table()
                schema.tables[table.name] = table
            else:
                raise ParseError(f"Expected 'table', got '{token[1]}'", token[2], token[3])
        
        return schema
    
    def _parse_annotation(self) -> tuple:
        """Parse @name = value"""
        token = self.expect('ANNOTATION')
        name = token[1][1:]  # Remove @ prefix
        self.expect('EQUALS')
        value = self._parse_value()
        return name, value
    
    def _parse_value(self) -> Any:
        """Parse a value (number, float, string, identifier)."""
        token = self.current()
        
        if token[0] == 'FLOAT':
            self.advance()
            return float(token[1])
        
        if token[0] == 'NUMBER':
            self.advance()
            return int(token[1])
        
        if token[0] == 'STRING':
            self.advance()
            return token[1][1:-1]  # Remove quotes
        
        if token[0] == 'IDENT' or token[0] == 'KEYWORD':
            self.advance()
            return token[1]
        
        raise ParseError(f"Expected value, got {token[0]}", token[2], token[3])
    
    def _parse_table(self) -> Table:
        """Parse: table Name { @sync_interval = X  config { } state { } status { } }"""
        self.expect('KEYWORD', 'table')
        name_token = self.expect('IDENT')
        table = Table(name=name_token[1])
        
        self.expect('LBRACE')
        
        # Check for table-level annotations
        while self.current()[0] == 'ANNOTATION':
            ann_name, ann_value = self._parse_annotation()
            if ann_name == 'sync_interval':
                table.sync_interval_ms = ann_value
        
        # Parse sections
        while self.current()[0] != 'RBRACE':
            token = self.current()
            
            if token[0] == 'KEYWORD' and token[1] == 'config':
                self.current_section = SectionType.CONFIG
                fields = self._parse_section('config')
                table.config_fields.extend(fields)
            
            elif token[0] == 'KEYWORD' and token[1] == 'state':
                self.current_section = SectionType.STATE
                fields = self._parse_section('state')
                table.state_fields.extend(fields)
            
            elif token[0] == 'KEYWORD' and token[1] == 'status':
                self.current_section = SectionType.STATUS
                fields = self._parse_section('status')
                table.status_fields.extend(fields)
            
            else:
                raise ParseError(f"Expected 'config', 'state', or 'status', got '{token[1]}'", token[2], token[3])
        
        self.expect('RBRACE')
        return table
    
    def _parse_section(self, section_name: str) -> List[Field]:
        """Parse: section_name { field; field; ... }"""
        self.expect('KEYWORD', section_name)
        self.expect('LBRACE')
        
        fields = []
        while self.current()[0] != 'RBRACE':
            token = self.current()
            if token[0] == 'KEYWORD' and token[1] in self.TYPES:
                field = self._parse_field()
                fields.append(field)
            else:
                raise ParseError(f"Expected type, got '{token[1]}'", token[2], token[3])
        
        self.expect('RBRACE')
        return fields
    
    def _parse_field(self) -> Field:
        """Parse field: TYPE[N]? NAME (= DEFAULT)? ;"""
        type_token = self.advance()
        base_type = type_token[1]
        array_size = None
        
        # Check for array syntax: type[N]
        if self.current()[0] == 'LBRACKET':
            self.advance()
            size_token = self.expect('NUMBER')
            array_size = int(size_token[1])
            self.expect('RBRACKET')
        
        name_token = self.expect('IDENT')
        field_name = name_token[1]
        
        default = None
        if self.current()[0] == 'EQUALS':
            self.advance()
            default = self._parse_value()
        
        self.expect('SEMICOLON')
        
        return Field(
            name=field_name,
            type=base_type,
            array_size=array_size,
            default=default,
            section=self.current_section
        )


def parse_schema(text: str) -> Schema:
    """Parse a .sds schema file and return a Schema object."""
    tokenizer = Tokenizer(text)
    parser = Parser(list(tokenizer))
    return parser.parse()


def parse_file(path: str) -> Schema:
    """Parse a .sds schema file from disk."""
    with open(path, 'r') as f:
        return parse_schema(f.read())

