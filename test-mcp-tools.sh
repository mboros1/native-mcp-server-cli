#!/bin/bash

# Test MCP tools with headless CLI

echo "Starting MCP Bridge Server with dynamic tools in console mode..."
echo "========================================="

# Kill any existing server on port 3000
lsof -ti:3000 | xargs kill -9 2>/dev/null

# Start the server in background with console output
DEBUG_CONSOLE=true node server/mcp-bridge-server-dynamic.js &
SERVER_PID=$!

# Wait for server to start
sleep 5

echo ""
echo "Server started with PID: $SERVER_PID"
echo "========================================="
echo ""

# Function to run headless CLI test
run_test() {
    local test_name="$1"
    local command="$2"
    
    echo "TEST: $test_name"
    echo "Command: $command"
    echo "---"
    
    cd src
    echo "$command" | ./bin/headless-cli --port 3000
    cd ..
    
    echo ""
    echo "========================================="
    echo ""
    sleep 2
}

# Test 1: List files in src directory
run_test "List files in src directory" "list files in the src directory"

# Test 2: List files in server directory  
run_test "List files in server directory" "use filesystem tools to show what's in the server directory"

# Test 3: Get recent GitHub commits
run_test "Get recent GitHub commits" "use github tools to show the 5 most recent commits in the mboros1/native-mcp-server-cli repository"

# Test 4: Search for JSON files
run_test "Search for JSON files" "use filesystem tools to find all .json files in the project"

# Test 5: Get GitHub repo info
run_test "Get info about this GitHub repo" "use github tools to get information about the mboros1/native-mcp-server-cli repository"

# Kill the server
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null

echo "Tests completed!"