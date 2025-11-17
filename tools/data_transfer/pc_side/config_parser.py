#!/usr/bin/env python3
"""
Configuration Parser for Rf_board_config.h

This module extracts USER_CHUNK_SIZE and other configuration values from
Rf_board_config.h to ensure synchronization between ESP32 and PC-side scripts.

Usage:
    from config_parser import get_user_chunk_size, get_board_config
    
    chunk_size = get_user_chunk_size()
    config = get_board_config()
"""

import os
import re
from pathlib import Path


def find_rf_board_config():
    """Locate Rf_board_config.h by searching upward from this script."""
    script_dir = Path(__file__).resolve().parent

    for base_dir in [script_dir] + list(script_dir.parents):
        for candidate in (
            base_dir / "src" / "Rf_board_config.h",
            base_dir / "Rf_board_config.h",
            base_dir / "include" / "Rf_board_config.h",
        ):
            if candidate.exists():
                return str(candidate)
    return None


def parse_board_config(config_file_path=None):
    """
    Parse Rf_board_config.h and extract all relevant configuration values.
    
    Args:
        config_file_path: Optional path to Rf_board_config.h. If None, auto-detect.
    
    Returns:
        Dictionary containing extracted configuration values:
        {
            'USER_CHUNK_SIZE': int,
            'DEFAULT_CHUNK_SIZE': int,
            'RF_BOARD_DEFAULT_CHUNK': int,
            'RF_BOARD_USB_RX_BUFFER': int,
            'found_path': str
        }
    """
    if config_file_path is None:
        config_file_path = find_rf_board_config()
    
    if not config_file_path or not os.path.exists(config_file_path):
        raise FileNotFoundError(
            f"Could not locate Rf_board_config.h. "
            f"Searched path: {config_file_path}"
        )
    
    config = {
        'found_path': config_file_path,
        'USER_CHUNK_SIZE': None,
        'DEFAULT_CHUNK_SIZE': None,
        'RF_BOARD_DEFAULT_CHUNK': None,
        'RF_BOARD_USB_RX_BUFFER': None,
    }
    
    # Read the entire file
    with open(config_file_path, 'r') as f:
        content = f.read()
    
    # Remove comments to avoid false matches
    # Remove single-line comments
    content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)
    # Remove multi-line comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    
    # Extract defines with various patterns
    # Pattern 1: #define MACRO value
    # Pattern 2: #define MACRO (expression)
    
    def extract_define_value(macro_name, text):
        """Extract the numeric value of a #define macro."""
        # Match: #define MACRO_NAME value
        pattern = rf'#\s*define\s+{macro_name}\s+(\(?[^\n]+\)?)'
        match = re.search(pattern, text)
        
        if match:
            value_str = match.group(1).strip()
            
            # Remove parentheses if present
            value_str = value_str.strip('()')
            
            # Try to resolve references to other macros
            # e.g., "RF_BOARD_DEFAULT_CHUNK" -> look up its value
            if value_str.isidentifier() and not value_str.isdigit():
                return extract_define_value(value_str, text)
            
            # Try to evaluate as integer
            try:
                # Handle hex values
                if value_str.startswith('0x') or value_str.startswith('0X'):
                    return int(value_str, 16)
                # Handle regular integers
                return int(value_str)
            except ValueError:
                # If it's an expression or reference we couldn't resolve
                return value_str
        
        return None
    
    # Extract each configuration value
    config['RF_BOARD_DEFAULT_CHUNK'] = extract_define_value('RF_BOARD_DEFAULT_CHUNK', content)
    config['RF_BOARD_USB_RX_BUFFER'] = extract_define_value('RF_BOARD_USB_RX_BUFFER', content)
    config['DEFAULT_CHUNK_SIZE'] = extract_define_value('DEFAULT_CHUNK_SIZE', content)
    config['USER_CHUNK_SIZE'] = extract_define_value('USER_CHUNK_SIZE', content)
    
    # Resolve USER_CHUNK_SIZE if it references DEFAULT_CHUNK_SIZE
    if config['USER_CHUNK_SIZE'] is None or config['USER_CHUNK_SIZE'] == 'DEFAULT_CHUNK_SIZE':
        if config['DEFAULT_CHUNK_SIZE'] is not None:
            config['USER_CHUNK_SIZE'] = config['DEFAULT_CHUNK_SIZE']
        elif config['RF_BOARD_DEFAULT_CHUNK'] is not None:
            config['USER_CHUNK_SIZE'] = config['RF_BOARD_DEFAULT_CHUNK']
    
    if config['DEFAULT_CHUNK_SIZE'] is None or config['DEFAULT_CHUNK_SIZE'] == 'RF_BOARD_DEFAULT_CHUNK':
        if config['RF_BOARD_DEFAULT_CHUNK'] is not None:
            config['DEFAULT_CHUNK_SIZE'] = config['RF_BOARD_DEFAULT_CHUNK']
    
    return config


def get_user_chunk_size(config_file_path=None, default=220):
    """
    Extract USER_CHUNK_SIZE from Rf_board_config.h.
    
    Args:
        config_file_path: Optional path to Rf_board_config.h. If None, auto-detect.
        default: Default value to return if USER_CHUNK_SIZE cannot be determined.
    
    Returns:
        int: The USER_CHUNK_SIZE value, or default if not found.
    """
    try:
        config = parse_board_config(config_file_path)
        chunk_size = config.get('USER_CHUNK_SIZE')
        
        if chunk_size is not None and isinstance(chunk_size, int):
            return chunk_size
        else:
            print(f"⚠️  Warning: Could not parse USER_CHUNK_SIZE from {config['found_path']}")
            print(f"   Using default value: {default}")
            return default
    
    except FileNotFoundError as e:
        print(f"⚠️  Warning: {e}")
        print(f"   Using default CHUNK_SIZE: {default}")
        return default
    except Exception as e:
        print(f"⚠️  Warning: Error parsing config file: {e}")
        print(f"   Using default CHUNK_SIZE: {default}")
        return default


def get_board_config(config_file_path=None, verbose=False):
    """
    Get all board configuration values from Rf_board_config.h.
    
    Args:
        config_file_path: Optional path to Rf_board_config.h. If None, auto-detect.
        verbose: If True, print configuration details.
    
    Returns:
        Dictionary with configuration values, or None if parsing failed.
    """
    try:
        config = parse_board_config(config_file_path)
        
        if verbose:
            print("\n=== Board Configuration (from Rf_board_config.h) ===")
            print(f"Config file: {config['found_path']}")
            print(f"USER_CHUNK_SIZE: {config.get('USER_CHUNK_SIZE', 'Not found')}")
            print(f"DEFAULT_CHUNK_SIZE: {config.get('DEFAULT_CHUNK_SIZE', 'Not found')}")
            print(f"RF_BOARD_DEFAULT_CHUNK: {config.get('RF_BOARD_DEFAULT_CHUNK', 'Not found')}")
            print(f"RF_BOARD_USB_RX_BUFFER: {config.get('RF_BOARD_USB_RX_BUFFER', 'Not found')}")
            print("=" * 55 + "\n")
        
        return config
    
    except Exception as e:
        if verbose:
            print(f"❌ Error getting board configuration: {e}")
        return None


if __name__ == "__main__":
    """Test the configuration parser."""
    print("Testing Configuration Parser\n")
    
    # Test 1: Find config file
    config_path = find_rf_board_config()
    if config_path:
        print(f"✅ Found Rf_board_config.h at: {config_path}\n")
    else:
        print("❌ Could not find Rf_board_config.h\n")
        exit(1)
    
    # Test 2: Parse config
    config = get_board_config(verbose=True)
    
    # Test 3: Get chunk size
    chunk_size = get_user_chunk_size()
    print(f"Extracted USER_CHUNK_SIZE: {chunk_size} bytes")
    
    # Validate
    if chunk_size and isinstance(chunk_size, int) and 128 <= chunk_size <= 512:
        print("✅ USER_CHUNK_SIZE is valid")
    else:
        print("⚠️  USER_CHUNK_SIZE may be out of expected range (128-512 bytes)")
