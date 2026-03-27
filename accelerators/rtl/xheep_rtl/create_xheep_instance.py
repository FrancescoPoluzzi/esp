#!/usr/bin/env python3

# Copyright (c) 2011-2024 Columbia University, System Level Design Group
# SPDX-License-Identifier: Apache-2.0

"""
Create multiple instances of X-HEEP accelerator for ESP.

This script creates numbered instances of the X-HEEP accelerator,
allowing multiple X-HEEP cores to be integrated into an ESP SoC.
"""

import os
import sys
import shutil
import re
from pathlib import Path


def get_script_dir():
    """Get the directory where this script is located (xheep base folder)."""
    return Path(__file__).parent.resolve()


def get_esp_root():
    """Get the ESP root directory."""
    return get_script_dir().parent.parent.parent



def native_flow():
    """
    Create a new X-HEEP instance for the native flow.
    """
    esp_root = get_esp_root()
    
    # Ask for the desired name
    new_name = input("Enter the desired name for the new accelerator [xheep1]: ")
    if not new_name:
        new_name = "xheep1"
    
    # Create a new directory
    new_accel_dir = esp_root / "accelerators" / "rtl" / f"{new_name}_rtl"
    if new_accel_dir.exists():
        print(f"Error: Directory {new_accel_dir} already exists.")
        return False
    
    # Copy the contents of accelerators/rtl/xheep_rtl
    source_dir = esp_root / "accelerators" / "rtl" / "xheep_rtl"
    
    def custom_ignore(path, names):
        ignored = set()
        
        app_dir = source_dir / "sw" / "linux" / "app"
        if Path(path).resolve() == app_dir.resolve():
            for name in names:
                if (Path(path) / name).is_dir():
                    ignored.add(name)
                
        return ignored

    shutil.copytree(source_dir, new_accel_dir, ignore=custom_ignore)
    
    # Correct the first line of the Makefile
    makefile_path = new_accel_dir / "Makefile"
    with open(makefile_path, 'r') as f:
        lines = f.readlines()
    lines[0] = f"ACCEL_NAME := {new_name}\n"
    with open(makefile_path, 'w') as f:
        f.writelines(lines)
        
    for subdir in ["hw", "sw"]:
        # topdown=False ensures we process leaf directories first before their parents are renamed
        for dirpath, dirnames, filenames in os.walk(new_accel_dir / subdir, topdown=False):
            
            # 1. Process files (Content replacement and renaming)
            for filename in filenames:
                file_path = Path(dirpath) / filename
                
                # Update file content
                with open(file_path, 'r', errors='ignore') as f:
                    content = f.read()
                
                if subdir == "hw":
                    # Specific replacements for module names in 'hw' folder
                    content = content.replace("xheep_boot_controller_dma", f"{new_name}_boot_controller_dma")
                    content = content.replace("xheep_rtl_basic_dma", f"{new_name}_rtl_basic_dma")
                else: # For 'sw' and potentially other subdirectories, keep general replacement
                    content = content.replace("xheep", new_name)
                    content = content.replace("XHEEP", new_name.upper())
                
                with open(file_path, 'w') as f:
                    f.write(content)
                
                # Rename file
                if "xheep" in filename:
                    new_filename = filename.replace("xheep", new_name)
                    os.rename(file_path, Path(dirpath) / new_filename)
            # 2. Process directories (Renaming)
            for dirname in dirnames:
                if "xheep" in dirname:
                    old_dir_path = Path(dirpath) / dirname
                    new_dirname = dirname.replace("xheep", new_name)
                    new_dir_path = Path(dirpath) / new_dirname
                    
                    os.rename(old_dir_path, new_dir_path)

    # Rename top-level verilog files
    for ext in [".sverilog", ".verilog"]:
        old_file = new_accel_dir / f"xheep{ext}"
        new_file = new_accel_dir / f"{new_name}{ext}"
        if old_file.exists():
            os.rename(old_file, new_file)
            
    print(f"\nSuccessfully created accelerator '{new_name}' in {new_accel_dir}")
    return True

def main():
    success = native_flow()
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
