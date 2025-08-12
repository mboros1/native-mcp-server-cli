# VS Code Dev Containers & GitHub Codespaces

This project supports development in VS Code Dev Containers and GitHub Codespaces, allowing you to run the full terminal UI in a browser!

## 🚀 Quick Start

### GitHub Codespaces (Browser-based)
1. Click the green "Code" button on GitHub
2. Select "Codespaces" tab
3. Click "Create codespace on main"
4. Wait for the container to build (~3-5 minutes first time)
5. The terminal UI will work in the integrated terminal!

### VS Code Dev Containers (Local)
1. Install Docker Desktop
2. Install the "Dev Containers" extension in VS Code
3. Open this project in VS Code
4. Click the popup "Reopen in Container" or use Command Palette: "Dev Containers: Reopen in Container"
5. Wait for the container to build

## 📦 What's Included

- **C++ Development**: GCC, CMake, Make, GDB, LLDB
- **Libraries**: FTXUI (terminal UI), spdlog, fmt
- **Node.js 20**: For the server component
- **Debugging**: Full debugging support with LLDB
- **IntelliSense**: Complete C++ IntelliSense with compile_commands.json

## 🎮 Running the Application

### Terminal UI Mode
```bash
cd src
./bin/demo --port 3000
```

### With Node.js Server
```bash
# Terminal 1: Start the server
node server/mcp-bridge-server-jsonrpc.js --console

# Terminal 2: Run the TUI
cd src
./bin/demo --port 3000
```

### Using the CLI wrapper
```bash
./cli.js
```

## 🐛 Debugging

The devcontainer is configured for debugging:

1. Set breakpoints in VS Code
2. Press F5 or go to Run and Debug
3. Select "Debug TUI (CodeLLDB)"
4. The TUI will run in the terminal with debugging active

## 🎨 Terminal UI in Browser

The FTXUI terminal UI renders perfectly in:
- GitHub Codespaces browser terminal
- VS Code integrated terminal
- VS Code terminal in browser (vscode.dev)

The terminal emulation supports:
- Full 256 colors
- Unicode characters
- Mouse interaction
- Responsive resizing

## 🔧 Configuration

Two devcontainer configurations are provided:

1. **devcontainer.json** - Uses pre-built Ubuntu C++ image
2. **devcontainer-dockerfile.json** - Custom Dockerfile with all dependencies

To switch configurations:
- Rename desired config to `devcontainer.json`
- Rebuild container

## 📝 Environment Variables

Set in your Codespace/devcontainer settings:
- `ANTHROPIC_API_KEY` - For Claude API
- `MOONSHOT_API_KEY` - For Kimi API  
- `OPENAI_API_KEY` - For OpenAI API
- `DEBUG_CONSOLE=true` - Enable console logging

## 🚧 Limitations

- File watching may be slower in containers
- Some advanced terminal features may vary by browser
- Debugging performance depends on connection speed

## 💡 Tips

- Use `make -j$(nproc)` for faster builds
- The terminal UI works best in fullscreen terminal
- Port forwarding is automatic for servers
- All data persists in `.data/` and `.logs/`