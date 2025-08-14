/**
 * MCP Server Manager
 * Manages multiple MCP server connections based on configuration
 */

import fs from 'fs';
import path from 'path';
import MCPClient from './mcpClient.js';
import { log } from './logger.js';

class MCPServerManager {
    constructor() {
        this.servers = new Map(); // name -> MCPClient instance
        this.tools = new Map();   // tool_name -> { serverName, tool }
        this.config = null;
    }

    /**
     * Load MCP servers configuration from file
     */
    async loadConfig(configPath = 'mcp-servers.json') {
        try {
            const configFile = path.resolve(configPath);
            const configData = fs.readFileSync(configFile, 'utf-8');
            this.config = JSON.parse(configData);
            log(`Loaded MCP config from ${configFile}`);
            return this.config;
        } catch (err) {
            log(`Failed to load MCP config: ${err.message}`);
            // Return default empty config
            return { mcpServers: {} };
        }
    }

    /**
     * Initialize all enabled MCP servers
     */
    async initializeServers() {
        if (!this.config) {
            await this.loadConfig();
        }

        const servers = this.config.mcpServers || {};
        const initPromises = [];

        for (const [name, serverConfig] of Object.entries(servers)) {
            if (serverConfig.enabled !== false) {
                log(`Initializing MCP server: ${name}`);
                initPromises.push(this.initializeServer(name, serverConfig));
            } else {
                log(`Skipping disabled MCP server: ${name}`);
            }
        }

        const results = await Promise.allSettled(initPromises);
        
        // Log results
        results.forEach((result, index) => {
            const serverName = Object.keys(servers)[index];
            if (result.status === 'fulfilled') {
                log(`✓ Successfully initialized ${serverName}`);
            } else {
                log(`✗ Failed to initialize ${serverName}: ${result.reason}`);
            }
        });

        // Discover and register tools from all connected servers
        await this.discoverTools();
        
        return this.servers;
    }

    /**
     * Initialize a single MCP server
     */
    async initializeServer(name, config) {
        const client = new MCPClient();
        
        try {
            // Expand environment variables in args
            const args = config.args.map(arg => 
                arg.replace(/\$\{([^}]+)\}/g, (_, key) => process.env[key] || '')
            );

            // Set environment variables
            const env = { ...process.env };
            if (config.env) {
                for (const [key, value] of Object.entries(config.env)) {
                    // Expand ${VAR} syntax
                    env[key] = value.replace(/\$\{([^}]+)\}/g, (_, k) => process.env[k] || '');
                }
            }

            // Connect to the server
            await client.connectToProcess(config.command, args, env);
            
            // Store the client
            this.servers.set(name, client);
            
            log(`Connected to MCP server '${name}'`);
            return client;
        } catch (err) {
            log(`Failed to connect to MCP server '${name}': ${err.message}`);
            throw err;
        }
    }

    /**
     * Discover and register tools from all connected servers
     */
    async discoverTools() {
        this.tools.clear();
        
        for (const [serverName, client] of this.servers) {
            try {
                const response = await client.listTools();
                const tools = response.tools || [];
                
                log(`Discovered ${tools.length} tools from ${serverName}`);
                
                // Register each tool with server prefix
                for (const tool of tools) {
                    const prefixedName = `${serverName}_${tool.name}`;
                    this.tools.set(prefixedName, {
                        serverName,
                        originalName: tool.name,
                        tool
                    });
                    log(`  - Registered tool: ${prefixedName}`);
                }
            } catch (err) {
                log(`Failed to discover tools from ${serverName}: ${err.message}`);
            }
        }
        
        return this.tools;
    }

    /**
     * Execute a tool on the appropriate server
     */
    async executeTool(toolName, args) {
        const toolInfo = this.tools.get(toolName);
        
        if (!toolInfo) {
            // Try without prefix for backward compatibility
            for (const [name, info] of this.tools) {
                if (info.originalName === toolName) {
                    toolInfo = info;
                    break;
                }
            }
            
            if (!toolInfo) {
                throw new Error(`Tool not found: ${toolName}`);
            }
        }
        
        const client = this.servers.get(toolInfo.serverName);
        if (!client) {
            throw new Error(`Server not connected: ${toolInfo.serverName}`);
        }
        
        return await client.executeTool(toolInfo.originalName, args);
    }

    /**
     * Get all available tools
     */
    getAllTools() {
        const tools = [];
        
        for (const [name, info] of this.tools) {
            tools.push({
                name,
                description: info.tool.description,
                inputSchema: info.tool.inputSchema,
                server: info.serverName
            });
        }
        
        return tools;
    }

    /**
     * Get tool definition
     */
    getToolDefinition(toolName) {
        const toolInfo = this.tools.get(toolName);
        
        if (!toolInfo) {
            // Try without prefix
            for (const [name, info] of this.tools) {
                if (info.originalName === toolName) {
                    return {
                        name,
                        ...info.tool
                    };
                }
            }
            return null;
        }
        
        return {
            name: toolName,
            ...toolInfo.tool
        };
    }

    /**
     * Add a new server dynamically
     */
    async addServer(name, config) {
        if (this.servers.has(name)) {
            throw new Error(`Server '${name}' already exists`);
        }
        
        await this.initializeServer(name, config);
        await this.discoverTools();
    }

    /**
     * Remove a server
     */
    async removeServer(name) {
        const client = this.servers.get(name);
        if (client) {
            client.close();
            this.servers.delete(name);
            
            // Remove tools from this server
            for (const [toolName, info] of this.tools) {
                if (info.serverName === name) {
                    this.tools.delete(toolName);
                }
            }
        }
    }

    /**
     * Shutdown all servers
     */
    async shutdown() {
        for (const [name, client] of this.servers) {
            log(`Shutting down MCP server: ${name}`);
            client.close();
        }
        this.servers.clear();
        this.tools.clear();
    }

    /**
     * Get server status
     */
    getStatus() {
        const status = {
            servers: {},
            toolCount: this.tools.size
        };
        
        for (const [name, client] of this.servers) {
            status.servers[name] = {
                connected: client.connected,
                tools: Array.from(this.tools.values())
                    .filter(info => info.serverName === name)
                    .length
            };
        }
        
        return status;
    }
}

// Singleton instance
let manager = null;

export function getMCPServerManager() {
    if (!manager) {
        manager = new MCPServerManager();
    }
    return manager;
}

export default MCPServerManager;