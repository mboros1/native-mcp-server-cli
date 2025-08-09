#!/bin/bash

# CI-friendly test runner (no colors, structured output)
# Returns 0 if all tests pass, 1 if any fail

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
    
    if ./$test_exec > /tmp/test_output_$$.txt 2>&1; then
        echo "PASS"
        PASSED_TESTS+=("$test_name")
    else
        echo "FAIL"
        FAILED_TESTS+=("$test_name")
        # Store the output for later
        FAILED_OUTPUT+=("$(cat /tmp/test_output_$$.txt)")
    fi
    
    rm -f /tmp/test_output_$$.txt
}

# Build all tests first
echo "Building tests..."
if ! make -s test-persist test-state test-input test-events; then
    echo "ERROR: Failed to build tests"
    exit 1
fi
echo ""

# Run each test
run_test "Chat Persistence" "test-persist"
run_test "State Manager" "test-state"
run_test "Input Handler" "test-input"
run_test "Event System" "test-events"

# Summary
echo ""
echo "================================"
echo "Test Summary"
echo "================================"
echo "Passed: ${#PASSED_TESTS[@]}/$TOTAL"
echo "Failed: ${#FAILED_TESTS[@]}/$TOTAL"

if [ ${#PASSED_TESTS[@]} -gt 0 ]; then
    echo ""
    echo "Passed tests:"
    for test in "${PASSED_TESTS[@]}"; do
        echo "  ✓ $test"
    done
fi

if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    for test in "${FAILED_TESTS[@]}"; do
        echo "  ✗ $test"
    done
    
    echo ""
    echo "================================"
    echo "Failed Test Output"
    echo "================================"
    
    for i in "${!FAILED_TESTS[@]}"; do
        echo ""
        echo "--------------------------------"
        echo "${FAILED_TESTS[$i]} output:"
        echo "--------------------------------"
        echo "${FAILED_OUTPUT[$i]}" | head -100
        if [ $(echo "${FAILED_OUTPUT[$i]}" | wc -l) -gt 100 ]; then
            echo "... (output truncated after 100 lines)"
        fi
    done
    
    echo ""
    echo "================================"
    echo "TESTS FAILED"
    echo "================================"
    exit 1
else
    echo ""
    echo "================================"
    echo "ALL TESTS PASSED"
    echo "================================"
    exit 0
fi