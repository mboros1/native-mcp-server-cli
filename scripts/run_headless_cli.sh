#!/bin/bash

# Script to run headless CLI with automatic server management
# Starts the server, runs the CLI, then cleans up

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
PORT=${PORT:-3000}
HOST=${HOST:-127.0.0.1}
CONFIG=${CONFIG:-tests/test_config.json}

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Change to project root
cd "$PROJECT_ROOT"

# Function to cleanup on exit
cleanup() {
    if [ ! -z "$SERVER_PID" ]; then
        echo -e "${YELLOW}Stopping server (PID: $SERVER_PID)...${NC}"
        kill $SERVER_PID 2>/dev/null
        wait $SERVER_PID 2>/dev/null
        echo -e "${GREEN}Server stopped${NC}"
    fi
}

# Set up trap to cleanup on exit
trap cleanup EXIT INT TERM

# Check if headless-cli binary exists
if [ ! -f "src/bin/headless-cli" ]; then
    echo -e "${YELLOW}Building headless-cli...${NC}"
    (cd src && make bin/headless-cli)
    if [ $? -ne 0 ]; then
        echo -e "${RED}Failed to build headless-cli${NC}"
        exit 1
    fi
fi

# Start the server
echo -e "${GREEN}Starting JSON-RPC server on port $PORT...${NC}"
if [ "$DEBUG_CONSOLE" = "true" ]; then
    # Run with console output for debugging
    PORT=$PORT DEBUG_CONSOLE=true USE_AGENT_MODE="$USE_AGENT_MODE" DEBUG_AGENTS="$DEBUG_AGENTS" node server/mcp-bridge-server-jsonrpc.js &
    SERVER_PID=$!
else
    # Run with file logging only
    PORT=$PORT USE_AGENT_MODE="$USE_AGENT_MODE" DEBUG_AGENTS="$DEBUG_AGENTS" node server/mcp-bridge-server-jsonrpc.js > .logs/server-headless.log 2>&1 &
    SERVER_PID=$!
fi

# Give server time to start
sleep 2

# Check if server is still running
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo -e "${RED}Server failed to start. Check .logs/server-headless.log for details${NC}"
    exit 1
fi

echo -e "${GREEN}Server started with PID: $SERVER_PID${NC}"

# Run the headless CLI
echo -e "${GREEN}Starting headless CLI...${NC}"
echo "----------------------------------------"

# Pass all arguments to the CLI
src/bin/headless-cli --host "$HOST" --port "$PORT" --config "$CONFIG" "$@"
CLI_EXIT_CODE=$?

echo "----------------------------------------"
echo -e "${GREEN}Headless CLI exited with code: $CLI_EXIT_CODE${NC}"

# Cleanup will happen automatically via trap
exit $CLI_EXIT_CODE