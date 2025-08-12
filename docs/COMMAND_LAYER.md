# Command Layer Architecture

## Overview

The command processing layer is modular and reusable across different frontends (TUI, headless CLI, tests).

## Components

### 1. InputHandler (`src/core/input_handler.cpp`)
- **Core command processor** - Contains all command logic
- **ProcessCommand(string)** - Main entry point for all commands
- Handles all commands: `/model`, `/think`, `/tools`, `/servers`, `/list`, `/load`, `/delete`, `/new`, `/sync`, `/help`, etc.
- Manages state, configuration, and server communication
- Works with any frontend that can call `ProcessCommand()`

### 2. HeadlessApplication (`src/core/headless_application.cpp`)
- **Wrapper around InputHandler** for non-TUI usage
- **ExecuteCommand(string)** - Calls `InputHandler::ProcessCommand()` internally
- Provides synchronous command execution with timeout
- Returns structured results (success, response, type)
- Used by tests and headless CLI

### 3. Frontend Implementations

#### TUI Application (`src/main.cpp`)
```cpp
// Direct use of InputHandler
input_handler_->ProcessCommand(user_input);
```

#### Headless CLI (`src/headless_cli.cpp`)
```cpp
// Uses HeadlessApplication wrapper
app.ExecuteCommand(line, timeout);
// All commands work: /model, /think, /tools, etc.
```

#### Integration Tests (`tests/test_*.cpp`)
```cpp
// Also uses HeadlessApplication
app.ExecuteCommand("/tools", 5s);
```

## Command Flow

```
User Input → Frontend → InputHandler::ProcessCommand() → Server Communication
                ↓                                              ↓
            HeadlessApplication                          JSON-RPC/Legacy
            (for non-TUI)                                   Protocol
```

## Key Design Principles

1. **Single Source of Truth**: All command logic lives in `InputHandler`
2. **Frontend Agnostic**: Commands work the same in TUI, CLI, or tests
3. **Consistent Interface**: Same commands available everywhere
4. **Modular**: Easy to add new frontends without duplicating logic

## Adding a New Frontend

To create a new frontend, you have two options:

### Option 1: Use HeadlessApplication (Recommended)
```cpp
HeadlessApplication app;
app.Initialize("config.json");
app.Connect("127.0.0.1", 4000);
app.Start();

// Execute any command
auto result = app.ExecuteCommand("/tools", 5s);
if (result.success) {
    // Handle response
}
```

### Option 2: Use InputHandler Directly
```cpp
StateManager state;
InputHandler handler(state, tools);

// Process commands directly
handler.ProcessCommand("/model kimi");
```

## Available Commands

All commands from `InputHandler::ProcessCommand()`:

- `/help` - Show help
- `/model <kimi|o3>` - Choose backend model  
- `/think <level>` - Set reasoning effort
- `/tools` - List available tools
- `/servers` - Show connected servers
- `/clear` - Clear conversation
- `/list` - List saved conversations
- `/load N` - Load conversation N
- `/delete N` - Delete conversation N
- `/new` - Start new conversation
- `/sync` - Check history sync
- `/exit`, `/quit`, `/q` - Exit application

## Running the Headless CLI

### With Automatic Server Management
```bash
# From src directory
make run-headless

# From project root
./headless

# With custom settings
PORT=4000 ./scripts/run_headless_cli.sh
```

### Manual Server Control
```bash
# Start server
node server/mcp-bridge-server-jsonrpc.js &

# Run CLI
src/bin/headless-cli --port 3000

# Stop server when done
kill $SERVER_PID
```

## Benefits of This Architecture

1. **No Code Duplication**: Command logic written once, used everywhere
2. **Consistent Behavior**: Commands work identically across all frontends
3. **Easy Testing**: HeadlessApplication makes integration testing simple
4. **Scriptable**: Headless CLI enables automation and scripting
5. **Maintainable**: Changes to commands only need to be made in one place