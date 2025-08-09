#!/bin/bash

echo "Testing MCP CLI..."

# Test 1: Standalone mode
echo "1. Testing standalone mode (Ctrl+C twice to exit):"
./src/demo
echo ""

# Test 2: With MCP server
echo "2. Testing with MCP server (type /tools to test):"
node cli.js

echo "Tests complete!"