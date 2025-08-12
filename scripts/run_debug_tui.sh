#!/bin/bash

# Script to run TUI in debug mode with Node.js server and debug server for VSCode
# Usage: ./scripts/run_debug_tui.sh [--port PORT]

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Configuration
DEBUG_PORT=${DEBUG_PORT:-1234}
SERVER_PORT=${SERVER_PORT:-3000}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --port)
            DEBUG_PORT="$2"
            shift 2
            ;;
        --server-port)
            SERVER_PORT="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--port DEBUG_PORT] [--server-port SERVER_PORT]"
            exit 1
            ;;
    esac
done

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    
    # Kill Node.js server if running
    if [ ! -z "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo -e "${YELLOW}Stopping Node.js server (PID: $SERVER_PID)...${NC}"
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    
    # Kill debug server if running
    if [ ! -z "$DEBUG_PID" ] && kill -0 "$DEBUG_PID" 2>/dev/null; then
        echo -e "${YELLOW}Stopping debug server (PID: $DEBUG_PID)...${NC}"
        kill "$DEBUG_PID" 2>/dev/null || true
    fi
    
    echo -e "${GREEN}Cleanup complete${NC}"
}

# Set up trap for cleanup
trap cleanup EXIT INT TERM

# Change to project root
cd "$PROJECT_ROOT"

# Build debug version if needed
echo -e "${GREEN}Building debug version...${NC}"
make -C src debug-demo

# Check if debug binary exists
if [ ! -f "src/bin/demo-debug" ]; then
    echo -e "${RED}Failed to build debug binary${NC}"
    exit 1
fi

# Start Node.js server
echo -e "${GREEN}Starting Node.js server on port $SERVER_PORT...${NC}"
node server/mcp-bridge-server-jsonrpc.js &
SERVER_PID=$!

# Wait for server to start
echo -e "${GREEN}Waiting for server to start...${NC}"
for i in {1..10}; do
    if nc -z localhost "$SERVER_PORT" 2>/dev/null; then
        echo -e "${GREEN}Server is ready${NC}"
        break
    fi
    if [ $i -eq 10 ]; then
        echo -e "${RED}Server failed to start${NC}"
        exit 1
    fi
    sleep 1
done

# Check for debug server - try multiple locations
DEBUGSERVER_PATHS=(
    "/Applications/Xcode.app/Contents/SharedFrameworks/LLDB.framework/Versions/A/Resources/debugserver"
    "/Library/Developer/CommandLineTools/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/debugserver"
    "$(xcode-select -p)/Library/PrivateFrameworks/LLDB.framework/Versions/A/Resources/debugserver"
)

DEBUG_SERVER=""
for path in "${DEBUGSERVER_PATHS[@]}"; do
    if [ -x "$path" ]; then
        DEBUG_SERVER="$path"
        echo -e "${GREEN}Found debugserver at: $path${NC}"
        break
    fi
done

# If no debugserver found, try lldb-server
if [ -z "$DEBUG_SERVER" ]; then
    if command -v lldb-server &> /dev/null; then
        DEBUG_SERVER="lldb-server"
        echo -e "${GREEN}Using lldb-server${NC}"
    else
        echo -e "${RED}No debug server found!${NC}"
        echo "Please install Xcode Command Line Tools:"
        echo "  xcode-select --install"
        echo "Or use lldb directly:"
        echo "  lldb src/bin/demo-debug"
        echo "  (lldb) process launch"
        exit 1
    fi
fi

# Create launch configuration for VSCode if it doesn't exist
VSCODE_LAUNCH_FILE="$PROJECT_ROOT/.vscode/launch.json"
if [ ! -f "$VSCODE_LAUNCH_FILE" ]; then
    echo -e "${GREEN}Creating VSCode launch configuration...${NC}"
    cat > "$VSCODE_LAUNCH_FILE" << EOF
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Attach to TUI Debug Server",
            "type": "lldb",
            "request": "attach",
            "program": "\${workspaceFolder}/src/bin/demo-debug",
            "attachCommands": ["gdb-remote $DEBUG_PORT"],
            "stopOnEntry": false,
            "preLaunchTask": "Start Debug Server"
        },
        {
            "name": "Debug TUI (Direct)",
            "type": "lldb",
            "request": "launch",
            "program": "\${workspaceFolder}/src/bin/demo-debug",
            "args": [],
            "cwd": "\${workspaceFolder}/src",
            "stopOnEntry": false,
            "environment": [],
            "preLaunchTask": "Build Debug"
        }
    ]
}
EOF
    echo -e "${GREEN}Created $VSCODE_LAUNCH_FILE${NC}"
fi

# Create tasks.json if it doesn't exist
VSCODE_TASKS_FILE="$PROJECT_ROOT/.vscode/tasks.json"
if [ ! -f "$VSCODE_TASKS_FILE" ]; then
    echo -e "${GREEN}Creating VSCode tasks configuration...${NC}"
    cat > "$VSCODE_TASKS_FILE" << EOF
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build Debug",
            "type": "shell",
            "command": "make",
            "args": ["-C", "src", "debug-demo"],
            "group": {
                "kind": "build",
                "isDefault": false
            },
            "problemMatcher": ["\$gcc"]
        },
        {
            "label": "Start Debug Server",
            "type": "shell",
            "command": "\${workspaceFolder}/scripts/run_debug_tui.sh",
            "args": ["--port", "$DEBUG_PORT"],
            "isBackground": true,
            "problemMatcher": {
                "pattern": {
                    "regexp": "^(Listening on port|Debug server ready)",
                    "line": 1
                },
                "background": {
                    "activeOnStart": true,
                    "beginsPattern": "^Starting debug server",
                    "endsPattern": "^(Listening on port|Debug server ready)"
                }
            }
        }
    ]
}
EOF
    echo -e "${GREEN}Created $VSCODE_TASKS_FILE${NC}"
fi

echo "----------------------------------------"
echo -e "${GREEN}Starting debug server...${NC}"
echo -e "${YELLOW}Debug server will listen on port: $DEBUG_PORT${NC}"
echo ""
echo -e "${GREEN}To connect from VSCode:${NC}"
echo "  1. Open VSCode in the project directory"
echo "  2. Go to Run and Debug (Cmd+Shift+D)"
echo "  3. Select 'Attach to TUI Debug Server' from the dropdown"
echo "  4. Press F5 or click the green play button"
echo ""
echo -e "${GREEN}Alternatively, connect manually with lldb:${NC}"
echo "  lldb src/bin/demo-debug"
echo "  (lldb) gdb-remote $DEBUG_PORT"
echo "  (lldb) continue"
echo "----------------------------------------"

# Start the debug server with the TUI app
cd src
if [[ "$DEBUG_SERVER" == *"debugserver"* ]]; then
    echo -e "${GREEN}Starting debugserver on port $DEBUG_PORT...${NC}"
    echo -e "${YELLOW}TUI will connect to Node.js server on port $SERVER_PORT${NC}"
    # debugserver syntax: debugserver host:port program [args]
    "$DEBUG_SERVER" 127.0.0.1:$DEBUG_PORT bin/demo-debug --port $SERVER_PORT &
    DEBUG_PID=$!
elif [ "$DEBUG_SERVER" = "lldb-server" ]; then
    echo -e "${GREEN}Starting lldb-server on port $DEBUG_PORT...${NC}"
    echo -e "${YELLOW}TUI will connect to Node.js server on port $SERVER_PORT${NC}"
    # lldb-server syntax: lldb-server gdbserver host:port -- program [args]
    lldb-server gdbserver 127.0.0.1:$DEBUG_PORT -- bin/demo-debug --port $SERVER_PORT &
    DEBUG_PID=$!
else
    echo -e "${RED}Unknown debug server type: $DEBUG_SERVER${NC}"
    exit 1
fi

echo -e "${GREEN}Debug server started with PID: $DEBUG_PID${NC}"
echo -e "${YELLOW}Waiting for debugger to connect...${NC}"

# Wait for debug server to exit
wait $DEBUG_PID 2>/dev/null
DEBUG_EXIT_CODE=$?

if [ $DEBUG_EXIT_CODE -eq 0 ]; then
    echo -e "${GREEN}Debug session ended normally${NC}"
else
    echo -e "${YELLOW}Debug session ended with code: $DEBUG_EXIT_CODE${NC}"
fi