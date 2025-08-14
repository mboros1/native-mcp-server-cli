/**
 * MCP Client for connecting to external MCP servers
 * Implements the MCP protocol to communicate with filesystem server
 */

import net from 'net';
import { EventEmitter } from 'events';
import { log } from './logger.js';

class MCPClient extends EventEmitter {
    constructor() {
        super();
        this.socket = null;
        this.requestId = 1;
        this.pendingRequests = new Map();
        this.connected = false;
        this.messageBuffer = '';
    }

    /**
     * Connect to MCP server via stdio (for filesystem server)
     * The filesystem server is already running, we need to communicate with it
     */
    async connectToProcess(command, args = [], env = process.env) {
        const { spawn } = await import('child_process');
        
        log(`Connecting to MCP server: ${command} ${args.join(' ')}`);
        
        this.process = spawn(command, args, {
            stdio: ['pipe', 'pipe', 'pipe'],
            env: env
        });

        // Handle stdout (responses from server)
        this.process.stdout.on('data', (data) => {
            this.handleData(data.toString());
        });

        // Handle stderr (error messages)
        this.process.stderr.on('data', (data) => {
            log(`MCP server stderr: ${data}`);
        });

        // Handle process exit
        this.process.on('exit', (code) => {
            log(`MCP server exited with code ${code}`);
            this.connected = false;
            this.emit('disconnected');
        });

        // Initialize connection
        await this.initialize();
    }

    /**
     * Connect to existing MCP server via TCP
     */
    async connectToTCP(host, port) {
        return new Promise((resolve, reject) => {
            this.socket = net.createConnection({ host, port }, () => {
                log(`Connected to MCP server at ${host}:${port}`);
                this.connected = true;
                this.emit('connected');
                resolve();
            });

            this.socket.on('data', (data) => {
                this.handleData(data.toString());
            });

            this.socket.on('error', (err) => {
                log(`MCP client error: ${err.message}`);
                reject(err);
            });

            this.socket.on('close', () => {
                this.connected = false;
                this.emit('disconnected');
            });
        });
    }

    /**
     * Handle incoming data from MCP server
     */
    handleData(data) {
        this.messageBuffer += data;
        
        // Process complete JSON-RPC messages
        const lines = this.messageBuffer.split('\n');
        this.messageBuffer = lines.pop() || '';
        
        for (const line of lines) {
            if (line.trim()) {
                try {
                    const message = JSON.parse(line);
                    this.handleMessage(message);
                } catch (err) {
                    log(`Failed to parse MCP message: ${line}`);
                }
            }
        }
    }

    /**
     * Handle parsed JSON-RPC message
     */
    handleMessage(message) {
        // Handle responses to our requests
        if (message.id && this.pendingRequests.has(message.id)) {
            const { resolve, reject } = this.pendingRequests.get(message.id);
            this.pendingRequests.delete(message.id);
            
            if (message.error) {
                reject(new Error(message.error.message || 'Unknown error'));
            } else {
                resolve(message.result);
            }
        }
        
        // Handle notifications from server
        if (!message.id && message.method) {
            this.emit('notification', message);
        }
    }

    /**
     * Send JSON-RPC request to MCP server
     */
    async sendRequest(method, params = {}) {
        const id = this.requestId++;
        const request = {
            jsonrpc: '2.0',
            id,
            method,
            params
        };

        return new Promise((resolve, reject) => {
            this.pendingRequests.set(id, { resolve, reject });
            
            const message = JSON.stringify(request) + '\n';
            
            if (this.process) {
                this.process.stdin.write(message);
            } else if (this.socket) {
                this.socket.write(message);
            } else {
                reject(new Error('Not connected to MCP server'));
            }
            
            // Timeout after 30 seconds
            setTimeout(() => {
                if (this.pendingRequests.has(id)) {
                    this.pendingRequests.delete(id);
                    reject(new Error('Request timeout'));
                }
            }, 30000);
        });
    }

    /**
     * Initialize MCP connection
     */
    async initialize() {
        try {
            const result = await this.sendRequest('initialize', {
                protocolVersion: '0.1.0',
                capabilities: {},
                clientInfo: {
                    name: 'mcp-bridge-server',
                    version: '1.0.0'
                }
            });
            
            log(`MCP server initialized: ${JSON.stringify(result)}`);
            this.connected = true;
            this.emit('initialized', result);
            return result;
        } catch (err) {
            log(`Failed to initialize MCP server: ${err.message}`);
            throw err;
        }
    }

    /**
     * List available tools from MCP server
     */
    async listTools() {
        return await this.sendRequest('tools/list');
    }

    /**
     * Execute a tool on MCP server
     */
    async executeTool(name, args) {
        return await this.sendRequest('tools/call', {
            name,
            arguments: args
        });
    }

    /**
     * Close connection
     */
    close() {
        if (this.process) {
            this.process.kill();
            this.process = null;
        }
        if (this.socket) {
            this.socket.end();
            this.socket = null;
        }
        this.connected = false;
    }
}

export default MCPClient;