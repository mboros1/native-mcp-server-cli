#!/bin/bash

# Simple test runner for C++ executable tests
# Returns 0 if all tests pass, 1 if any fail

# Get the directory where this script is located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Project root is one level up from scripts/
PROJECT_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Change to src directory for building
cd "$PROJECT_ROOT/src" || exit 1

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

FAILED_TESTS=()
PASSED_TESTS=()
FAILED_OUTPUT=()
TOTAL=0

echo "================================"
echo "Running Unit Tests"
echo "================================"

# Function to run a test
run_test() {
    local test_name=$1
    local test_exec=$2
    
    TOTAL=$((TOTAL + 1))
    echo -n "Running $test_name... "
    
    # Run test and capture exit code
    $test_exec > /tmp/test_output_$$.txt 2>&1
    local exit_code=$?
    
    if [ $exit_code -eq 0 ]; then
        echo -e "${GREEN}PASS${NC}"
        PASSED_TESTS+=("$test_name")
    else
        # Check if it was a segfault
        if [ $exit_code -eq 139 ]; then
            echo -e "${RED}FAIL (Segmentation fault)${NC}"
        elif [ $exit_code -eq 134 ]; then
            echo -e "${RED}FAIL (Assertion failed)${NC}"
        else
            echo -e "${RED}FAIL (exit code: $exit_code)${NC}"
        fi
        FAILED_TESTS+=("$test_name")
        # Store the output for later
        if [ -f /tmp/test_output_$$.txt ]; then
            FAILED_OUTPUT+=("$(cat /tmp/test_output_$$.txt)")
        else
            FAILED_OUTPUT+=("(No output captured - test may have crashed)")
        fi
    fi
    
    rm -f /tmp/test_output_$$.txt
}

# Build all tests first
echo "Building tests..."
# Build core tests
make -s bin/test-persist bin/test-state bin/test-input bin/test-events || exit 1

# Build JSON-RPC tests (now all fixed!)
make -s bin/test-jsonrpc-builder bin/test-configurable-mock bin/test-tool-events || exit 1

# Build new JSON-RPC integration tests
make -s bin/test-mcp-client-jsonrpc bin/test-jsonrpc-procedures bin/test-headless-jsonrpc || exit 1
echo ""

# Run each test from bin directory (inside src/)
# Core tests
run_test "Chat Persistence" "bin/test-persist"
run_test "State Manager" "bin/test-state"
run_test "Input Handler" "bin/test-input"
run_test "Event System" "bin/test-events"

# JSON-RPC infrastructure tests
run_test "JSON-RPC Builder" "bin/test-jsonrpc-builder"
run_test "Configurable Mock Server" "bin/test-configurable-mock"
run_test "Tool Events" "bin/test-tool-events"

# JSON-RPC integration tests
run_test "MCP Client JSON-RPC" "bin/test-mcp-client-jsonrpc"
run_test "JSON-RPC Procedures" "bin/test-jsonrpc-procedures"
run_test "Headless JSON-RPC Integration" "bin/test-headless-jsonrpc"

# Summary
echo ""
echo "================================"
echo "Test Summary"
echo "================================"
echo -e "Passed: ${GREEN}${#PASSED_TESTS[@]}${NC}/$TOTAL"
echo -e "Failed: ${RED}${#FAILED_TESTS[@]}${NC}/$TOTAL"

if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo ""
    echo -e "${RED}Failed tests:${NC}"
    for i in "${!FAILED_TESTS[@]}"; do
        echo ""
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo -e "${RED}${FAILED_TESTS[$i]} output:${NC}"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo "${FAILED_OUTPUT[$i]}" | head -50
        if [ $(echo "${FAILED_OUTPUT[$i]}" | wc -l) -gt 50 ]; then
            echo "... (output truncated)"
        fi
    done
    exit 1
else
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi