#!/usr/bin/env node

import { Server } from '@modelcontextprotocol/sdk/server/index.js';
import { StdioServerTransport } from '@modelcontextprotocol/sdk/server/stdio.js';
import { ListResourcesRequestSchema, ListToolsRequestSchema, CallToolRequestSchema } from '@modelcontextprotocol/sdk/types.js';
import { readFile } from 'fs/promises';
import { unlinkSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import net from 'net';
import { createInterface } from 'readline';
import { logger } from './lib/logger.js';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Check if we should use socket mode
const socketPath = process.argv.includes('--socket') 
  ? process.argv[process.argv.indexOf('--socket') + 1]
  : null;

// Create MCP server instance
const server = new Server({
  name: 'native-mcp-cli',
  version: '0.1.0',
}, {
  capabilities: {
    resources: {},
    tools: {},
  },
});

// Load tool definitions from config
async function loadTools() {
  try {
    const configPath = join(__dirname, 'config.json');
    logger.debug(`Loading config from: ${configPath}`);
    const configData = await readFile(configPath, 'utf-8');
    const config = JSON.parse(configData);
    logger.info(`Loaded ${config.tools?.length || 0} tools from config`);
    return config.tools || [];
  } catch (error) {
    logger.error('Failed to load config:', error);
    return [];
  }
}

// List available tools
server.setRequestHandler(ListToolsRequestSchema, async () => {
  const tools = await loadTools();
  
  return {
    tools: tools.map(tool => ({
      name: tool.name,
      description: tool.description,
      inputSchema: {
        type: 'object',
        properties: tool.parameters || {},
        required: tool.required || [],
      },
    })),
  };
});

// Handle tool calls
server.setRequestHandler(CallToolRequestSchema, async (request) => {
  const { name, arguments: args } = request.params;
  
  // For now, just echo back - real implementation would execute tools
  console.error(`Tool called: ${name} with args:`, args);
  
  // TODO: Implement actual tool execution
  // This is where you'd integrate with the file system, run commands, etc.
  
  return {
    content: [
      {
        type: 'text',
        text: `Tool ${name} executed with arguments: ${JSON.stringify(args)}`,
      },
    ],
  };
});

// List available resources (optional)
server.setRequestHandler(ListResourcesRequestSchema, async () => {
  return {
    resources: [
      {
        uri: 'file:///config',
        name: 'Configuration',
        description: 'Current MCP server configuration',
        mimeType: 'application/json',
      },
    ],
  };
});

// Start the server
async function main() {
  logger.info('MCP Server starting...');
  logger.info('Socket path:', socketPath);
  
  if (socketPath) {
    // Socket mode - listen on Unix domain socket
    const socketServer = net.createServer((socket) => {
      logger.info('Client connected to socket');
      
      const rl = createInterface({
        input: socket,
        output: socket,
        terminal: false
      });
      
      rl.on('line', async (line) => {
        try {
          const request = JSON.parse(line);
          logger.debug('Received request:', request);
          
          // Handle JSON-RPC requests
          let response;
          if (request.method === 'tools/list') {
            logger.info('Handling tools/list request');
            const result = await server.requestHandlers.get(ListToolsRequestSchema)?.(request);
            response = {
              jsonrpc: '2.0',
              id: request.id,
              result
            };
          } else if (request.method === 'tools/call') {
            logger.info('Handling tools/call request');
            const result = await server.requestHandlers.get(CallToolRequestSchema)?.(request);
            response = {
              jsonrpc: '2.0',
              id: request.id,
              result
            };
          } else {
            logger.warn(`Unknown method: ${request.method}`);
            response = {
              jsonrpc: '2.0',
              id: request.id,
              error: {
                code: -32601,
                message: 'Method not found'
              }
            };
          }
          
          logger.debug('Sending response:', response);
          socket.write(JSON.stringify(response) + '\n');
        } catch (error) {
          logger.error('Error handling request:', error);
        }
      });
      
      socket.on('error', (err) => {
        logger.error('Socket error:', err);
      });
      
      socket.on('end', () => {
        logger.info('Client disconnected');
      });
    });
    
    // Remove existing socket file if it exists
    try {
      fs.unlinkSync(socketPath);
    } catch (e) {}
    
    socketServer.listen(socketPath, () => {
      logger.info(`MCP Server listening on socket: ${socketPath}`);
    });
  } else {
    // Stdio mode - use MCP SDK transport
    const transport = new StdioServerTransport();
    await server.connect(transport);
    logger.info('MCP Server started on stdio');
  }
}

main().catch(error => {
  logger.error('Server error:', error);
  process.exit(1);
});