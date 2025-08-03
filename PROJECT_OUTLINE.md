# Native MCP Server CLI

## Project Overview

A high-performance Model Context Protocol (MCP) server implementation with native C++ CLI interface, combining the functionality of Claude Code with modern terminal UI capabilities.

### Key Features
- **Native Performance**: C++ FTXUI-based CLI for smooth, responsive terminal interactions
- **MCP Server**: Full implementation of the Model Context Protocol specification
- **File System Tools**: Built-in utilities for code exploration, editing, and version control
- **Dual Interface**: 
  - HTTP API for programmatic access (SDK-friendly)
  - Interactive CLI for power users
- **Node.js Integration**: Thin N-API wrapper exposing C++ functionality to JavaScript

## Architecture

```
┌─────────────────┐     ┌─────────────────┐
│   HTTP Client   │     │   CLI User      │
└────────┬────────┘     └────────┬────────┘
         │                       │
         ▼                       ▼
┌─────────────────┐     ┌─────────────────┐
│   HTTP Server   │     │   FTXUI CLI     │
│   (Node.js)     │     │   (C++)         │
└────────┬────────┘     └────────┬────────┘
         │                       │
         └───────────┬───────────┘
                     ▼
           ┌─────────────────┐
           │   N-API Bridge  │
           └─────────────────┘
                     │
                     ▼
           ┌─────────────────┐
           │   Core MCP      │
           │   Server Logic  │
           └─────────────────┘
```

## Technology Stack

- **C++20**: Core implementation language
- **FTXUI**: Terminal UI framework (header-only)
- **Node.js**: HTTP server and JavaScript SDK
- **N-API**: Node.js native addon interface
- **MCP Protocol**: Model Context Protocol for AI tool integration

## Project Structure

```
native-mcp-server-cli/
├── src/                    # C++ source files
│   ├── main.cpp           # CLI entry point
│   ├── mcp/               # MCP server implementation
│   ├── tools/             # File system and git tools
│   └── napi/              # N-API bindings
├── deps/                   # Dependencies
│   └── ftxui/             # FTXUI headers
├── lib/                    # Node.js library
│   ├── server.js          # HTTP server
│   └── sdk.js             # JavaScript SDK
├── build/                  # Build artifacts
└── tests/                  # Test suites
```

## Build System

- **Make**: Primary build system for C++ components
- **pkg-config**: Dependency management for system libraries
- **npm**: Node.js package management and scripts

## Development Phases

### Phase 1: Foundation (Current)
- [x] Project structure setup
- [ ] Basic FTXUI CLI scaffold
- [ ] Simple file reading tool
- [ ] N-API hello-world binding

### Phase 2: Core MCP Implementation
- [ ] MCP protocol handler
- [ ] Tool registration system
- [ ] Basic file system tools (read, write, list)
- [ ] Error handling and logging

### Phase 3: Advanced Features
- [ ] Git integration
- [ ] Code search and grep functionality
- [ ] HTTP server implementation
- [ ] JavaScript SDK

### Phase 4: Polish & Performance
- [ ] CLI command palette
- [ ] Configuration system
- [ ] Performance optimization
- [ ] Documentation and examples

## Getting Started

```bash
# Install dependencies
make deps

# Build the CLI
cd src && make

# Run the demo
./demo
```

## Design Principles

1. **Performance First**: Native C++ for all performance-critical paths
2. **Clean Interfaces**: Clear separation between UI, logic, and protocol layers
3. **Extensibility**: Easy to add new tools and capabilities
4. **Developer Experience**: Beautiful, responsive CLI with modern features
5. **Protocol Compliance**: Full adherence to MCP specification

## Future Considerations

- WebAssembly build for browser-based usage
- Plugin system for custom tools
- Multi-language SDK support
- Cloud deployment options