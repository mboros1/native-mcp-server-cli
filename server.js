#!/usr/bin/env node

/**
 * MCP Server - Standalone server that can be accessed by any client
 */

import { createServer } from 'net';
import { WebSocketServer } from 'ws';
import { MCPServer } from './lib/mcp-server.js';
import { program } from 'commander';

program
  .name('mcp-server')
  .description('Model Context Protocol Server')
  .option('-p, --port <port>', 'TCP port for JSON-RPC', '3000')
  .option('-w, --ws-port <port>', 'WebSocket port', '3001')
  .option('--stdio', 'Use stdio instead of network')
  .parse();

const options = program.opts();

async function main() {
  const mcpServer = new MCPServer();
  await mcpServer.initialize();

  if (options.stdio) {
    // Stdio mode for direct CLI usage
    console.error('MCP Server started in stdio mode');
    mcpServer.handleStdio(process.stdin, process.stdout);
  } else {
    // TCP JSON-RPC server
    const tcpServer = createServer((socket) => {
      console.log('Client connected via TCP');
      mcpServer.handleConnection(socket);
      
      socket.on('end', () => {
        console.log('Client disconnected');
      });
    });

    tcpServer.listen(options.port, () => {
      console.log(`MCP Server listening on TCP port ${options.port}`);
    });

    // WebSocket server for web clients
    const wss = new WebSocketServer({ port: options.wsPort });
    
    wss.on('connection', (ws) => {
      console.log('Client connected via WebSocket');
      mcpServer.handleWebSocket(ws);
    });

    console.log(`WebSocket server listening on port ${options.wsPort}`);
  }
}

main().catch(err => {
  console.error('Server error:', err);
  process.exit(1);
});