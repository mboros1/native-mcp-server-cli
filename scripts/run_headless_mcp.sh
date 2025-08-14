#!/bin/bash

# Run headless CLI with dynamic MCP server
# This script starts the MCP bridge server and connects the headless CLI

set -e

# Configuration
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-3000}"
CONFIG="${CONFIG:-tests/test_config.json}"
SERVER_LOG=".logs/mcp-bridge-server.log"
CLI_LOG=".logs/headless-cli.log"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Ensure log directory exists
mkdir -p .logs

# Function to cleanup on exit
cleanup() {
    if [ ! -z "$SERVER_PID" ]; then
        echo -e "${YELLOW}Stopping server (PID: $SERVER_PID)...${NC}"
        kill $SERVER_PID 2>/dev/null || true
        wait $SERVER_PID 2>/dev/null || true
        echo -e "${GREEN}Server stopped${NC}"
    fi
}

# Set up trap to cleanup on exit
trap cleanup EXIT INT TERM

# Kill any existing server on the port
echo -e "${YELLOW}Checking for existing server on port $PORT...${NC}"
lsof -ti:$PORT | xargs kill -9 2>/dev/null || true
sleep 1

# Start the dynamic MCP bridge server
echo -e "${GREEN}Starting Dynamic MCP Bridge Server on port $PORT...${NC}"
if [ "$DEBUG_CONSOLE" = "true" ]; then
    # Run with console output
    PORT=$PORT DEBUG_CONSOLE=true USE_AGENT_MODE="$USE_AGENT_MODE" DEBUG_AGENTS="$DEBUG_AGENTS" node server/mcp-bridge-server-jsonrpc.js &
else
    # Run with file logging only
    PORT=$PORT USE_AGENT_MODE="$USE_AGENT_MODE" DEBUG_AGENTS="$DEBUG_AGENTS" node server/mcp-bridge-server-jsonrpc.js > "$SERVER_LOG" 2>&1 &
fi
SERVER_PID=$!

# Wait for server to be ready
echo -e "${YELLOW}Waiting for server to start...${NC}"
for i in {1..10}; do
    if lsof -Pi :$PORT -sTCP:LISTEN -t >/dev/null 2>&1; then
        echo -e "${GREEN}Server started with PID: $SERVER_PID${NC}"
        break
    fi
    if [ $i -eq 10 ]; then
        echo -e "${RED}Server failed to start. Check $SERVER_LOG for details.${NC}"
        exit 1
    fi
    sleep 1
done

# Additional wait for MCP servers to initialize
echo -e "${YELLOW}Waiting for MCP servers to initialize...${NC}"
sleep 2

# Start the headless CLI
echo -e "${GREEN}Starting headless CLI...${NC}"
echo "----------------------------------------"
src/bin/headless-cli --host "$HOST" --port "$PORT" --config "$CONFIG" "$@"
CLI_EXIT_CODE=$?
echo "----------------------------------------"

if [ $CLI_EXIT_CODE -ne 0 ]; then
    echo -e "${RED}Headless CLI exited with code: $CLI_EXIT_CODE${NC}"
    exit $CLI_EXIT_CODE
fi