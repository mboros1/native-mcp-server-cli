/**
 * TCP Integration for JSON-RPC Server
 * 
 * Integrates the JSON-RPC server with TCP sockets
 * Handles connection management, message framing, and abort signals
 */

import { log } from '../logger.js';

/**
 * TCP Socket Handler for JSON-RPC
 * 
 * Manages per-connection state and message handling
 */
export class TcpJsonRpcHandler {
    constructor(socket, jsonRpcServer) {
        this.socket = socket;
        this.jsonRpcServer = jsonRpcServer;
        this.clientId = `${socket.remoteAddress}:${socket.remotePort}`;
        this.buffer = '';
        this.abortController = null;
        
        this.setupHandlers();
    }
    
    /**
     * Set up socket event handlers
     */
    setupHandlers() {
        // Handle incoming data
        this.socket.on('data', (data) => {
            this.handleData(data);
        });
        
        // Handle socket close
        this.socket.on('close', () => {
            this.handleClose();
        });
        
        // Handle socket error
        this.socket.on('error', (err) => {
            this.handleError(err);
        });
        
        // Send initial hello
        this.sendHello();
    }
    
    /**
     * Send initial connection message
     */
    async sendHello() {
        // Send a JSON-RPC notification about connection
        const notification = {
            jsonrpc: '2.0',
            method: 'connection.established',
            params: {
                clientId: this.clientId,
                timestamp: Math.floor(Date.now() / 1000)
            }
        };
        
        this.sendMessage(JSON.stringify(notification));
        log(`Client connected: ${this.clientId}`);
    }
    
    /**
     * Handle incoming data
     */
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
    
    /**
     * Process a complete message
     */
    async processMessage(message) {
        log(`[${this.clientId}] Received: ${message.slice(0, 100)}...`);
        
        // Create abort controller for this request
        this.abortController = new AbortController();
        
        // Create context for handlers
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
                this.sendMessage(response);
            }
            
        } catch (err) {
            log(`[${this.clientId}] Processing error: ${err.message}`);
            
            // Send error response
            const errorResponse = {
                jsonrpc: '2.0',
                error: {
                    code: -32603,
                    message: 'Internal error',
                    data: err.message
                },
                id: null
            };
            
            this.sendMessage(JSON.stringify(errorResponse));
        } finally {
            this.abortController = null;
        }
    }
    
    /**
     * Send a message to the client
     */
    sendMessage(message) {
        if (this.socket.writable) {
            this.socket.write(message + '\n');
            log(`[${this.clientId}] Sent: ${message.slice(0, 100)}...`);
        }
    }
    
    /**
     * Handle socket close
     */
    handleClose() {
        log(`[${this.clientId}] Connection closed`);
        
        // Abort any active requests
        if (this.abortController) {
            this.abortController.abort();
        }
        
        // Cancel all active requests for this client
        this.jsonRpcServer.activeRequests.forEach((request, id) => {
            if (request.context && request.context.clientId === this.clientId) {
                this.jsonRpcServer.cancelRequest(id);
            }
        });
    }
    
    /**
     * Handle socket error
     */
    handleError(err) {
        log(`[${this.clientId}] Socket error: ${err.message}`);
    }
    
    /**
     * Abort current request
     */
    abort() {
        if (this.abortController) {
            this.abortController.abort();
            this.abortController = null;
            return true;
        }
        return false;
    }
}

/**
 * Create a TCP server with JSON-RPC handling
 * 
 * @param {net.Server} tcpServer - Node.js TCP server
 * @param {JsonRpcServer} jsonRpcServer - JSON-RPC server instance
 * @returns {net.Server} - Configured TCP server
 */
export function setupTcpJsonRpc(tcpServer, jsonRpcServer) {
    // Track active connections
    const connections = new Map();
    
    // Handle new connections
    tcpServer.on('connection', (socket) => {
        const handler = new TcpJsonRpcHandler(socket, jsonRpcServer);
        const clientId = handler.clientId;
        
        connections.set(clientId, handler);
        
        // Remove on close
        socket.on('close', () => {
            connections.delete(clientId);
        });
    });
    
    // Add server methods
    tcpServer.getConnections = () => connections;
    tcpServer.abortClient = (clientId) => {
        const handler = connections.get(clientId);
        return handler ? handler.abort() : false;
    };
    
    return tcpServer;
}

/**
 * Handle legacy message format migration
 * 
 * Converts old message format to JSON-RPC 2.0
 */
export function migrateLegacyMessage(message) {
    try {
        const parsed = JSON.parse(message);
        
        // Check if already JSON-RPC 2.0
        if (parsed.jsonrpc === '2.0') {
            return message;
        }
        
        // Convert legacy format
        let jsonRpcMessage = {
            jsonrpc: '2.0',
            id: parsed.id || Date.now()
        };
        
        // Map legacy message types to JSON-RPC methods
        switch (parsed.type) {
            case 'chat':
                jsonRpcMessage.method = 'chat.send';
                jsonRpcMessage.params = {
                    content: parsed.content || parsed.message,
                    model: parsed.model,
                    timeout: parsed.timeout,
                    reasoning_effort: parsed.reasoning_effort
                };
                break;
                
            case 'retry':
                jsonRpcMessage.method = 'chat.retry';
                jsonRpcMessage.params = {
                    content: parsed.content || parsed.originalMessage
                };
                break;
                
            case 'interrupt':
            case 'cancel':
                jsonRpcMessage.method = 'request.cancel';
                jsonRpcMessage.params = {};
                break;
                
            case 'tools':
                jsonRpcMessage.method = 'tools.list';
                jsonRpcMessage.params = {};
                break;
                
            case 'tool':
                jsonRpcMessage.method = 'tool.execute';
                jsonRpcMessage.params = {
                    name: parsed.tool || parsed.name,
                    arguments: parsed.args || parsed.arguments
                };
                break;
                
            case 'load':
                jsonRpcMessage.method = 'history.load';
                jsonRpcMessage.params = {
                    filename: parsed.filename || parsed.file
                };
                break;
                
            case 'sync':
                jsonRpcMessage.method = 'history.save';
                jsonRpcMessage.params = {};
                break;
                
            default:
                // Unknown type, return as-is
                return message;
        }
        
        return JSON.stringify(jsonRpcMessage);
        
    } catch (err) {
        // Not JSON or conversion failed
        return message;
    }
}