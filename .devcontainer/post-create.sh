#!/bin/bash

set -e

echo "📦 Installing system dependencies..."

# Update package lists
sudo apt-get update

# Install C++ build tools and libraries
sudo apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libspdlog-dev \
    libfmt-dev \
    bear \
    gdb \
    lldb

# Install FTXUI from source (not in Ubuntu repos)
echo "🎨 Building FTXUI..."
cd /tmp
git clone https://github.com/ArthurSonzogni/FTXUI.git
cd FTXUI
mkdir build && cd build
cmake .. -GNinja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFTXUI_BUILD_EXAMPLES=OFF \
    -DFTXUI_BUILD_TESTS=OFF \
    -DFTXUI_BUILD_DOCS=OFF
ninja
sudo ninja install
sudo ldconfig

# Create required directories first
echo "📁 Setting up directories..."
mkdir -p .data
mkdir -p .logs
mkdir -p .config

# Install Node.js dependencies
echo "📦 Installing Node.js dependencies..."
npm install

# Build the C++ project
echo "🔨 Building C++ project..."
cd src
make clean
make all

# Generate compile_commands.json for IntelliSense
echo "🔧 Generating compile_commands.json..."
make compile_commands || true
cd ..

echo "✅ Devcontainer setup complete!"
echo ""
echo "Quick start:"
echo "  - Run TUI: cd src && ./bin/demo"
echo "  - Run with server: ./cli.js"
echo "  - Debug: F5 in VS Code"
echo "  - Run tests: cd src && make test-all"