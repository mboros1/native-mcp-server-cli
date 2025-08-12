#!/bin/bash

# Script to run TUI in debug mode with Node.js server using lldb directly
# This is simpler than the debug server approach and works well for local debugging

set -e

# Colors for output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Configuration
SERVER_PORT=${SERVER_PORT:-3000}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Cleanup function
cleanup() {
    echo -e "${YELLOW}Cleaning up...${NC}"
    
    # Kill Node.js server if running
    if [ ! -z "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        echo -e "${YELLOW}Stopping Node.js server (PID: $SERVER_PID)...${NC}"
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
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

# Start Node.js server with console output for debugging
echo -e "${GREEN}Starting Node.js server on port $SERVER_PORT (with console output)...${NC}"
node server/mcp-bridge-server-jsonrpc.js --console &
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

echo "----------------------------------------"
echo -e "${GREEN}Starting TUI in lldb debugger${NC}"
echo ""
echo -e "${GREEN}Useful lldb commands:${NC}"
echo "  r                - Run the program"
echo "  b main           - Set breakpoint at main"
echo "  b file.cpp:123   - Set breakpoint at line"
echo "  c                - Continue execution"
echo "  n                - Next line"
echo "  s                - Step into"
echo "  p variable       - Print variable"
echo "  bt               - Show backtrace"
echo "  quit             - Exit debugger"
echo "----------------------------------------"

# Start lldb with the debug binary, passing the correct port
cd src
lldb bin/demo-debug -- --port $SERVER_PORT