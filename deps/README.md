# Dependencies Directory

This directory is intended to hold header-only libraries and other dependencies.

## FTXUI

FTXUI is a modern C++ terminal UI library. For development, you have two options:

1. **System-wide installation** (recommended for getting started):
   ```bash
   # macOS
   brew install ftxui
   
   # Ubuntu/Debian
   sudo apt install libftxui-dev
   
   # Using vcpkg
   vcpkg install ftxui
   ```

2. **Local copy** (for production/distribution):
   Clone FTXUI headers here when ready to bundle:
   ```bash
   git clone https://github.com/ArthurSonzogni/FTXUI.git ftxui
   ```

The Makefile currently uses `pkg-config` to find FTXUI, which works with system installations.