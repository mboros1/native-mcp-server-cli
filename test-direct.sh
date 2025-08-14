#!/bin/bash
PORT=3001 USE_AGENT_MODE=false DEBUG_CONSOLE=true timeout 10 node server/mcp-bridge-server-jsonrpc.js &
SERVER_PID=$!
sleep 3

# Test with curl
echo "Testing with direct API call..."
curl -X POST http://127.0.0.1:3001 \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools.list",
    "params": {}
  }'

kill $SERVER_PID 2>/dev/null
