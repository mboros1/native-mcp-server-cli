#!/bin/bash

# JavaScript test runner script
# Runs all server tests and reports results

echo "========================================="
echo "Running JavaScript Server Tests"
echo "========================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Track test results
FAILED_TESTS=()
PASSED_TESTS=()
TOTAL_TESTS=0

# Find all test files
TEST_DIR="server/test"
TEST_FILES=$(find $TEST_DIR -name "test-*.js" 2>/dev/null | sort)

if [ -z "$TEST_FILES" ]; then
    echo "No test files found in $TEST_DIR"
    exit 0
fi

# Run each test file
for TEST_FILE in $TEST_FILES; do
    TEST_NAME=$(basename $TEST_FILE .js)
    echo ""
    echo "Running: $TEST_NAME"
    echo "-----------------------------------------"
    
    # Run the test
    node $TEST_FILE
    EXIT_CODE=$?
    
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    
    if [ $EXIT_CODE -eq 0 ]; then
        echo -e "${GREEN}✓ $TEST_NAME passed${NC}"
        PASSED_TESTS+=("$TEST_NAME")
    else
        echo -e "${RED}✗ $TEST_NAME failed (exit code: $EXIT_CODE)${NC}"
        FAILED_TESTS+=("$TEST_NAME")
    fi
done

# Summary
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo "Total tests run: $TOTAL_TESTS"
echo -e "${GREEN}Passed: ${#PASSED_TESTS[@]}${NC}"
echo -e "${RED}Failed: ${#FAILED_TESTS[@]}${NC}"

if [ ${#FAILED_TESTS[@]} -gt 0 ]; then
    echo ""
    echo "Failed tests:"
    for TEST in "${FAILED_TESTS[@]}"; do
        echo "  - $TEST"
    done
    echo ""
    echo "JavaScript tests FAILED"
    exit 1
else
    echo ""
    echo "All JavaScript tests PASSED"
    exit 0
fi