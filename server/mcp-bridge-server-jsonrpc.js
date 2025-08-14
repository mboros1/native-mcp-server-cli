/**
 * MCP Bridge Server - JSON-RPC 2.0 Implementation
 * 
 * Refactored to use modular JSON-RPC architecture
 */

import net from 'net';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import { config } from 'dotenv';

// JSON-RPC modules
import { JsonRpcServer } from './lib/jsonrpc/JsonRpcServer.js';
import { registerAllProcedures } from './lib/jsonrpc/procedures.js';
import { registerAllProceduresDynamic } from './lib/jsonrpc/procedures-dynamic.js';
import { ApiManager } from './lib/jsonrpc/ApiManager.js';
import { ChatHistory } from './lib/jsonrpc/ChatHistory.js';
import { setupTcpJsonRpc, migrateLegacyMessage } from './lib/jsonrpc/TcpIntegration.js';
import { silenceConsole, log } from './lib/logger.js';

// Dynamic MCP tool loading
import { 
    initializeToolRouter, 
    getToolSystemStatus 
} from './tools/toolRouter-dynamic.js';

// CRITICAL: Silence all console output to prevent FTXUI corruption
// Unless DEBUG_CONSOLE is set for debugging
if (process.env.DEBUG_CONSOLE !== 'true') {
    silenceConsole();
}

// Load environment variables
config({ silent: true });

// Get current directory
const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

// Ensure required directories exist
const dataDir = path.join(__dirname, '../.data');
const logsDir = path.join(__dirname, '../.logs');

if (!fs.existsSync(dataDir)) {
    fs.mkdirSync(dataDir, { recursive: true });
}
if (!fs.existsSync(logsDir)) {
    fs.mkdirSync(logsDir, { recursive: true });
}

// Initialize services
const jsonRpcServer = new JsonRpcServer();
const apiManager = new ApiManager();
const chatHistory = new ChatHistory(dataDir);

// Load initial chat history from C++ file
chatHistory.loadFromFile();

// Initialize server with dynamic MCP support
async function initializeServer() {
    // Check if dynamic MCP is enabled (default: true)
    const USE_DYNAMIC_MCP = process.env.USE_DYNAMIC_MCP !== 'false';
    let procedures;
    
    if (USE_DYNAMIC_MCP) {
        try {
            log('Initializing dynamic MCP tools...');
            await initializeToolRouter();
            const status = await getToolSystemStatus();
            log(`MCP tools initialized: ${status.totalTools} tools available`);
            
            // Use dynamic procedures with MCP support
            procedures = registerAllProceduresDynamic(jsonRpcServer, {
                apiManager,
                chatHistory
            });
        } catch (err) {
            log(`Failed to initialize MCP tools: ${err.message}`);
            log('Falling back to static tools only');
            
            // Fall back to standard procedures
            procedures = registerAllProcedures(jsonRpcServer, {
                apiManager,
                chatHistory
            });
        }
    } else {
        // Use standard procedures when MCP is disabled
        procedures = registerAllProcedures(jsonRpcServer, {
            apiManager,
            chatHistory
        });
    }
    
    return procedures;
}

// Initialize the server (will set up procedures)
const proceduresPromise = initializeServer();

// Create enhanced TCP handler that supports both formats
class BridgeTcpHandler {
    constructor(socket, jsonRpcServer) {
        this.socket = socket;
        this.jsonRpcServer = jsonRpcServer;
        this.clientId = `${socket.remoteAddress}:${socket.remotePort}`;
        this.buffer = '';
        this.abortController = null;
        
        this.setupHandlers();
    }
    
    setupHandlers() {
        // Send welcome message (legacy format for now)
        this.socket.write(JSON.stringify({ 
            type: 'welcome', 
            message: 'Connected to MCP bridge server (JSON-RPC)' 
        }) + '\n');
        
        log(`Client connected: ${this.clientId}`);
        
        // Handle incoming data
        this.socket.on('data', (data) => {
            this.handleData(data);
        });
        
        // Handle socket close
        this.socket.on('close', () => {
            log(`Client disconnected: ${this.clientId}`);
            
            // Abort any active requests
            if (this.abortController) {
                this.abortController.abort();
            }
        });
        
        // Handle socket error
        this.socket.on('error', (err) => {
            log(`Socket error for ${this.clientId}: ${err.message}`);
        });
    }
    
    handleData(data) {
        // Append to buffer
        this.buffer += data.toString();
        
        // Process complete messages (newline delimited)
        let newlineIndex;
        while ((newlineIndex = this.buffer.indexOf('\n')) !== -1) {
            const message = this.buffer.slice(0, newlineIndex).trim();
            this.buffer = this.buffer.slice(newlineIndex + 1);
            
            if (message) {
                this.processMessage(message);
            }
        }
    }
    
    async processMessage(message) {
        log(`Received from ${this.clientId}: ${message.slice(0, 100)}...`);
        
        try {
            // Try to parse as JSON first
            const parsed = JSON.parse(message);
            
            // Check if it's already JSON-RPC 2.0
            if (parsed.jsonrpc === '2.0') {
                // Process as JSON-RPC
                await this.handleJsonRpc(message);
            } else {
                // Convert legacy format to JSON-RPC
                await this.handleLegacy(parsed);
            }
        } catch (err) {
            log(`Error processing message: ${err.message}`);
            
            // Send error in legacy format
            this.socket.write(JSON.stringify({
                type: 'error',
                message: err.message
            }) + '\n');
        }
    }
    
    async handleJsonRpc(message) {
        // Create abort controller for this request
        this.abortController = new AbortController();
        
        // Create context
        const context = {
            clientId: this.clientId,
            socket: this.socket,
            abortSignal: this.abortController.signal
        };
        
        try {
            // Process through JSON-RPC server
            const response = await this.jsonRpcServer.processMessage(message, context);
            
            // Send response if not a notification
            if (response) {
                this.socket.write(response + '\n');
                log(`Sent JSON-RPC response to ${this.clientId}`);
            }
        } finally {
            this.abortController = null;
        }
    }
    
    async handleLegacy(payload) {
        // Map legacy message types to JSON-RPC calls
        let jsonRpcRequest = {
            jsonrpc: '2.0',
            id: Date.now() // Generate ID for legacy requests
        };
        
        // Handle special commands
        if (payload.type === 'chat' && payload.content?.trim() === '/new') {
            jsonRpcRequest.method = 'history.rotate';
            jsonRpcRequest.params = {};
        } else if (payload.type === 'chat') {
            jsonRpcRequest.method = 'chat.send';
            jsonRpcRequest.params = {
                content: payload.content,
                model: payload.model || 'kimi',
                timeout: payload.timeout,
                reasoning_effort: payload.reasoning_effort
            };
        } else if (payload.type === 'retry') {
            jsonRpcRequest.method = 'chat.retry';
            jsonRpcRequest.params = {
                content: payload.originalMessage
            };
        } else if (payload.type === 'interrupt') {
            jsonRpcRequest.method = 'request.cancel';
            jsonRpcRequest.params = {};
        } else if (payload.type === 'reload' && payload.content === 'chat_history') {
            jsonRpcRequest.method = 'history.reload';
            jsonRpcRequest.params = {};
        } else if (payload.type === 'sync' && payload.content === 'request_history') {
            jsonRpcRequest.method = 'history.sync';
            jsonRpcRequest.params = {};
        } else if (payload.type === 'tool_list') {
            jsonRpcRequest.method = 'tools.list';
            jsonRpcRequest.params = {};
        } else if (payload.type === 'tool') {
            jsonRpcRequest.method = 'tool.execute';
            jsonRpcRequest.params = {
                name: payload.tool,
                arguments: payload.args
            };
        } else {
            // Unknown type
            log(`Unknown message type: ${payload.type}`);
            this.socket.write(JSON.stringify({
                type: 'error',
                message: `Unknown message type: ${payload.type}`
            }) + '\n');
            return;
        }
        
        // Process through JSON-RPC
        const jsonRpcMessage = JSON.stringify(jsonRpcRequest);
        log(`Converted legacy to JSON-RPC: ${jsonRpcMessage}`);
        
        // Create context
        this.abortController = new AbortController();
        const context = {
            clientId: this.clientId,
            socket: this.socket,
            abortSignal: this.abortController.signal
        };
        
        try {
            // Process the request
            const response = await this.jsonRpcServer.processMessage(jsonRpcMessage, context);
            
            if (response) {
                const parsed = JSON.parse(response);
                
                // Convert JSON-RPC response back to legacy format
                if (parsed.result) {
                    // Success - map based on original type
                    if (payload.type === 'chat' || payload.type === 'retry') {
                        // Chat response
                        this.socket.write(JSON.stringify({
                            type: 'response',
                            original: payload,
                            reply: parsed.result.reply,
                            timestamp: parsed.result.timestamp
                        }) + '\n');
                    } else if (payload.type === 'sync') {
                        // Sync response
                        this.socket.write(JSON.stringify({
                            type: 'sync_response',
                            ...parsed.result
                        }) + '\n');
                    } else if (payload.type === 'tool_list') {
                        // Tool list response
                        this.socket.write(JSON.stringify({
                            type: 'tool_list_response',
                            tools: parsed.result.tools
                        }) + '\n');
                    } else if (payload.type === 'tool') {
                        // Tool execution response
                        this.socket.write(JSON.stringify({
                            type: 'tool_response',
                            result: parsed.result.output,
                            success: parsed.result.success,
                            error: parsed.result.error
                        }) + '\n');
                    } else {
                        // Generic response
                        this.socket.write(JSON.stringify({
                            type: 'response',
                            ...parsed.result
                        }) + '\n');
                    }
                } else if (parsed.error) {
                    // Error response
                    const errorMessage = parsed.error.message || 'Request failed';
                    
                    // Check for specific error types
                    if (parsed.error.code === -32000) {
                        // Timeout error
                        this.socket.write(JSON.stringify({
                            type: 'timeout_error',
                            message: errorMessage,
                            canRetry: true,
                            originalMessage: payload.content
                        }) + '\n');
                    } else if (parsed.error.code === -32001) {
                        // Cancelled
                        this.socket.write(JSON.stringify({
                            type: 'system',
                            message: 'Request was cancelled'
                        }) + '\n');
                    } else {
                        // Generic error
                        this.socket.write(JSON.stringify({
                            type: 'error',
                            message: errorMessage
                        }) + '\n');
                    }
                }
            }
        } catch (err) {
            log(`Error in legacy handler: ${err.message}`);
            this.socket.write(JSON.stringify({
                type: 'error',
                message: err.message
            }) + '\n');
        } finally {
            this.abortController = null;
        }
    }
}

// Create TCP server
const server = net.createServer((socket) => {
    // Use our bridge handler that supports both formats
    new BridgeTcpHandler(socket, jsonRpcServer);
});

// Export functions for CLI integration
export async function startServer(port = 3000) {
    // Load chat history at startup
    chatHistory.loadFromFile();
    
    // Wait for procedures to be initialized (including MCP tools if enabled)
    await proceduresPromise;
    
    return new Promise((resolve, reject) => {
        server.listen(port, '127.0.0.1', () => {
            log(`MCP Bridge Server (JSON-RPC) started on port ${port}`);
            
            // Log configuration
            const models = apiManager.getAvailableModels();
            if (models.length > 0) {
                log(`Available models: ${models.join(', ')}`);
            } else {
                log('WARNING: No API keys configured. Set KIMI_API_KEY or OPENAI_API_KEY');
            }
            
            resolve();
        });

        server.on('error', (err) => {
            if (err.code === 'EADDRINUSE') {
                log(`Port ${port} is already in use`);
            }
            reject(err);
        });
    });
}

export function stopServer() {
    return new Promise((resolve) => {
        log('Server shutting down...');
        server.close(() => {
            log('Server closed');
            resolve();
        });
    });
}

// If run directly (not imported), start the server
if (import.meta.url === `file://${process.argv[1]}`) {
    const PORT = process.env.PORT || process.env.MCP_SERVER_PORT || 3000;
    
    startServer(PORT).then(() => {
        if (process.env.DEBUG_CONSOLE === 'true') {
            console.log(`MCP Bridge Server started on port ${PORT}`);
            console.log('Console output enabled - logs will appear here');
        }
    }).catch(err => {
        if (err.code === 'EADDRINUSE') {
            console.error(`\n❌ Error: Port ${PORT} is already in use`);
            console.error(`\nPlease try one of the following:`);
            console.error(`  1. Stop the process using port ${PORT}`);
            console.error(`  2. Set a different port using the PORT environment variable:`);
            console.error(`     PORT=3001 ${process.argv[1]}`);
            console.error(`  3. Or use MCP_SERVER_PORT environment variable:`);
            console.error(`     MCP_SERVER_PORT=3001 ${process.argv[1]}\n`);
        } else {
            console.error('Failed to start server:', err);
        }
        process.exit(1);
    });
    
    // Graceful shutdown
    process.on('SIGINT', () => {
        stopServer().then(() => {
            process.exit(0);
        });
    });
}

export default server;