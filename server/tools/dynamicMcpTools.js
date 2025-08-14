/**
 * Dynamic MCP Tools Integration
 * Dynamically loads and manages tools from multiple MCP servers
 */

import { getMCPServerManager } from '../lib/mcpServerManager.js';
import { toolLog } from '../lib/logger.js';

// Cache for the manager instance
let initialized = false;

/**
 * Initialize MCP servers from configuration
 */
export async function initializeMCPServers(configPath = 'mcp-servers.json') {
    if (initialized) {
        return getMCPServerManager();
    }

    const manager = getMCPServerManager();
    
    try {
        await manager.loadConfig(configPath);
        await manager.initializeServers();
        initialized = true;
        
        const status = manager.getStatus();
        toolLog(`MCP Servers initialized: ${JSON.stringify(status, null, 2)}`);
        
        return manager;
    } catch (err) {
        toolLog(`Failed to initialize MCP servers: ${err.message}`);
        throw err;
    }
}

/**
 * Get all available MCP tools
 */
export async function getMCPTools() {
    const manager = await initializeMCPServers();
    return manager.getAllTools();
}

/**
 * Execute an MCP tool
 */
export async function executeMCPTool(toolName, args) {
    const manager = await initializeMCPServers();
    
    try {
        const result = await manager.executeTool(toolName, args);
        toolLog(`MCP tool ${toolName} executed successfully`);
        return result;
    } catch (err) {
        toolLog(`MCP tool ${toolName} failed: ${err.message}`);
        throw err;
    }
}

/**
 * Dynamic tool wrapper that creates functions for each MCP tool
 */
export async function createDynamicToolWrappers() {
    const manager = await initializeMCPServers();
    const tools = manager.getAllTools();
    const wrappers = {};
    
    for (const tool of tools) {
        wrappers[tool.name] = async (args) => {
            return await executeMCPTool(tool.name, args);
        };
    }
    
    return wrappers;
}

/**
 * Get tool definitions for all MCP tools
 */
export async function getMCPToolDefinitions() {
    const manager = await initializeMCPServers();
    const tools = manager.getAllTools();
    
    return tools.map(tool => ({
        name: tool.name,
        description: tool.description,
        inputSchema: tool.inputSchema,
        server: tool.server
    }));
}

/**
 * Refresh MCP servers (reload config and reconnect)
 */
export async function refreshMCPServers() {
    const manager = getMCPServerManager();
    await manager.shutdown();
    initialized = false;
    return await initializeMCPServers();
}

/**
 * Get MCP server status
 */
export async function getMCPServerStatus() {
    const manager = await initializeMCPServers();
    return manager.getStatus();
}

// Export a ready-to-use tool registry
export const DYNAMIC_MCP_TOOLS = {
    /**
     * Initialize and get all tool implementations
     */
    async getImplementations() {
        await initializeMCPServers();
        return await createDynamicToolWrappers();
    },
    
    /**
     * Get all tool definitions
     */
    async getDefinitions() {
        return await getMCPToolDefinitions();
    },
    
    /**
     * Execute a tool by name
     */
    async execute(name, args) {
        return await executeMCPTool(name, args);
    }
};