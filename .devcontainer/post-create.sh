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

# Install Node.js dependencies
echo "📦 Installing Node.js dependencies..."
cd /workspace
npm install

# Build the C++ project
echo "🔨 Building C++ project..."
cd /workspace/src
make clean
make all

# Create required directories
echo "📁 Setting up directories..."
mkdir -p /workspace/.data
mkdir -p /workspace/.logs
mkdir -p /workspace/.config

# Generate compile_commands.json for IntelliSense
echo "🔧 Generating compile_commands.json..."
make compile_commands || true

echo "✅ Devcontainer setup complete!"
echo ""
echo "Quick start:"
echo "  - Run TUI: cd src && ./bin/demo"
echo "  - Run with server: ./cli.js"
echo "  - Debug: F5 in VS Code"
echo "  - Run tests: cd src && make test-all"