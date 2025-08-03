# Build Instructions

## Prerequisites

### macOS (Homebrew)
```bash
# Install FTXUI
brew install ftxui

# Install build tools (if not already installed)
brew install pkg-config
```

### Ubuntu/Debian
```bash
# Update package list
sudo apt update

# Install FTXUI and build tools
sudo apt install libftxui-dev pkg-config build-essential
```

### Fedora/RHEL
```bash
# Install FTXUI and build tools
sudo dnf install ftxui-devel pkgconfig gcc-c++ make
```

### Arch Linux
```bash
# Install FTXUI from AUR
yay -S ftxui
# or with paru
paru -S ftxui

# Install build tools
sudo pacman -S base-devel pkg-config
```

### Alpine Linux
```bash
# Install FTXUI and build tools
sudo apk add ftxui-dev pkgconf g++ make
```

## Building the Project

Once dependencies are installed, build the project:

```bash
cd src
make
./demo
```

## Troubleshooting

### pkg-config not finding FTXUI

If you get "Package 'ftxui' not found" errors:

1. **macOS**: Ensure Homebrew's pkg-config path is set:
   ```bash
   export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
   # For Apple Silicon Macs:
   export PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH"
   ```

2. **Linux**: Check if ftxui.pc is installed:
   ```bash
   find /usr -name "ftxui*.pc" 2>/dev/null
   ```

### Linking Errors

If pkg-config isn't available or doesn't work, you can manually specify flags:

```bash
# macOS (Intel)
g++ -std=c++20 main.cpp -o demo -I/usr/local/include -L/usr/local/lib -lftxui-screen -lftxui-dom -lftxui-component

# macOS (Apple Silicon)
g++ -std=c++20 main.cpp -o demo -I/opt/homebrew/include -L/opt/homebrew/lib -lftxui-screen -lftxui-dom -lftxui-component

# Linux (typical paths)
g++ -std=c++20 main.cpp -o demo -I/usr/include -L/usr/lib -lftxui-screen -lftxui-dom -lftxui-component
```