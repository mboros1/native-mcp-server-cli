#!/bin/bash

# Run JavaScript tests in isolated environment
# This simulates CI conditions more closely

echo "Setting up isolated test environment..."

# Create temporary directory
TEST_DIR=$(mktemp -d)
echo "Test directory: $TEST_DIR"

# Copy necessary files
cp -r server $TEST_DIR/
cp package*.json $TEST_DIR/

# Change to test directory
cd $TEST_DIR

# Install dependencies fresh
echo "Installing dependencies..."
npm install --silent
cd server
npm install --silent
cd ..

# Set environment like CI
export CI=true
export NODE_ENV=test
export DEBUG_CONSOLE=true

# Run the actual tests
echo "Running tests in isolated environment..."
echo "========================================="

# Run test-chat-processor specifically since it's failing
echo "Testing chat-processor..."
node server/test/test-chat-processor.js

# Run test-jsonrpc 
echo ""
echo "Testing jsonrpc..."
node server/test/test-jsonrpc.js

# Cleanup
cd /
rm -rf $TEST_DIR

echo "Isolated test complete"