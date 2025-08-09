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
    
    if $test_exec > /tmp/test_output_$$.txt 2>&1; then
        echo -e "${GREEN}PASS${NC}"
        PASSED_TESTS+=("$test_name")
    else
        echo -e "${RED}FAIL${NC}"
        FAILED_TESTS+=("$test_name")
        # Store the output for later
        FAILED_OUTPUT+=("$(cat /tmp/test_output_$$.txt)")
    fi
    
    rm -f /tmp/test_output_$$.txt
}

# Build all tests first
echo "Building tests..."
make -s test-persist test-state test-input test-events || exit 1
echo ""

# Run each test from bin directory (inside src/)
run_test "Chat Persistence" "bin/test-persist"
run_test "State Manager" "bin/test-state"
run_test "Input Handler" "bin/test-input"
run_test "Event System" "bin/test-events"

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