# Port Configuration

The MCP Bridge Server supports configurable ports to avoid conflicts with other services.

## Environment Variables

### PORT
Primary environment variable for setting the server port.
```bash
PORT=3001 ./scripts/run_headless_cli.sh
```

### MCP_SERVER_PORT (Legacy)
Alternative environment variable for backward compatibility.
```bash
MCP_SERVER_PORT=3001 ./scripts/run_headless_cli.sh
```

## Default Port
- Default: 3000
- Priority: PORT > MCP_SERVER_PORT > 3000

## Error Handling
If the specified port is already in use, you'll see a clear error message with suggestions:
```
❌ Error: Port 3000 is already in use

Please try one of the following:
  1. Stop the process using port 3000
  2. Set a different port using the PORT environment variable:
     PORT=3001 node server/mcp-bridge-server-jsonrpc.js
  3. Or use MCP_SERVER_PORT environment variable:
     MCP_SERVER_PORT=3001 node server/mcp-bridge-server-jsonrpc.js
```

## Examples

### Running with Agent Mode on Custom Port
```bash
PORT=3001 USE_AGENT_MODE=true DEBUG_AGENTS=true ./scripts/run_headless_cli.sh
```

### Running the Server Directly
```bash
PORT=3001 node server/mcp-bridge-server-jsonrpc.js
```

### Finding Process Using a Port
```bash
lsof -i :3000
```

### Killing Process on a Port
```bash
lsof -ti:3000 | xargs kill -9
```