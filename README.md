# Native MCP CLI

A high-performance Model Context Protocol (MCP) CLI with a native C++ terminal UI.

## Features

- ⚡ Native C++ terminal UI using FTXUI for blazing fast performance
- 🤖 AI chat integration (OpenAI, Anthropic, etc.)
- 🔧 MCP tool execution
- 📝 Command history and logging
- 🎨 Beautiful synthwave-inspired terminal aesthetics

## Installation

```bash
npm install -g @native/mcp-cli
```

Or build from source:

```bash
git clone https://github.com/yourusername/native-mcp-cli.git
cd native-mcp-cli
npm install
npm run build
```

## Usage

### Interactive Mode (with AI chat)
```bash
mcp-cli
```

### Standalone Mode (no AI, just TUI)
```bash
mcp-cli --standalone
```

### Commands

All commands start with `/`:

- `/help` or `/h` - Show help
- `/list` or `/ls` - List available tools
- `/dump` - Save screen to file
- `/exit` or `/q` - Exit application
- `/<tool>` - Show tool information

Any text without a slash is sent to the AI assistant.

## Architecture

```
┌─────────────────────┐
│   Node.js Layer     │ ← API connections, MCP protocol
└──────────┬──────────┘
           │ IPC
┌──────────▼──────────┐
│   C++ TUI Layer     │ ← Terminal UI, user interaction
└─────────────────────┘
```

## Development

```bash
# Build C++ binary
npm run build

# Run in development mode
npm run dev

# Clean build artifacts
npm run clean
```

## License

MIT