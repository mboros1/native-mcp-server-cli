/**
 * @file toolRouter-dynamic.js
 * @brief Dynamic MCP tool router with config-based loading
 * @author Native MCP Team
 * @date 2025
 * 
 * Enhanced tool router that dynamically loads tools from MCP servers
 * based on configuration file.
 */

import { listFiles, LIST_FILES_TOOL } from './listFiles.js';
import { DYNAMIC_MCP_TOOLS, initializeMCPServers, getMCPServerStatus } from './dynamicMcpTools.js';
import { toolLog } from '../lib/logger.js';

// Static tools (built-in)
const staticTools = {
    list_files: listFiles
};

// Dynamic tools will be loaded from MCP servers
let dynamicTools = {};
let dynamicToolDefinitions = [];

/**
 * Initialize the tool router
 */
export async function initializeToolRouter() {
    try {
        // Initialize MCP servers
        await initializeMCPServers();
        
        // Load dynamic tool implementations
        dynamicTools = await DYNAMIC_MCP_TOOLS.getImplementations();
        
        // Load dynamic tool definitions
        dynamicToolDefinitions = await DYNAMIC_MCP_TOOLS.getDefinitions();
        
        toolLog(`Tool router initialized with ${Object.keys(dynamicTools).length} dynamic tools`);
        
        return true;
    } catch (err) {
        toolLog(`Failed to initialize tool router: ${err.message}`);
        // Continue with static tools only
        return false;
    }
}

/**
 * Get all tool implementations
 */
export function getToolImplementations() {
    return {
        ...staticTools,
        ...dynamicTools
    };
}

/**
 * Get all available tool definitions
 */
export function getAvailableTools() {
    return [
        LIST_FILES_TOOL,
        ...dynamicToolDefinitions
    ];
}

/**
 * Execute a tool by name with given arguments
 * 
 * @param {string} toolName - Name of the tool to execute
 * @param {Object} args - Arguments to pass to the tool
 * @returns {Promise<any>} Tool execution result
 * @throws {Error} If tool not found or execution fails
 */
export async function executeTool(toolName, args) {
    // Check static tools first
    if (staticTools[toolName]) {
        try {
            toolLog(`[Tool] Executing static tool ${toolName}`);
            const result = await staticTools[toolName](args);
            toolLog(`[Tool] ${toolName} completed successfully`);
            return result;
        } catch (error) {
            toolLog(`[Tool] ${toolName} failed:`, error.message);
            throw error;
        }
    }
    
    // Check dynamic tools
    if (dynamicTools[toolName]) {
        try {
            toolLog(`[Tool] Executing dynamic MCP tool ${toolName}`);
            const result = await dynamicTools[toolName](args);
            toolLog(`[Tool] ${toolName} completed successfully`);
            return result;
        } catch (error) {
            toolLog(`[Tool] ${toolName} failed:`, error.message);
            throw error;
        }
    }
    
    // Try DYNAMIC_MCP_TOOLS.execute as fallback
    try {
        toolLog(`[Tool] Attempting to execute ${toolName} via MCP`);
        const result = await DYNAMIC_MCP_TOOLS.execute(toolName, args);
        toolLog(`[Tool] ${toolName} completed successfully`);
        return result;
    } catch (error) {
        throw new Error(`Unknown tool: ${toolName}`);
    }
}

/**
 * Get tool definition by name
 * 
 * @param {string} toolName - Name of the tool
 * @returns {Object|null} Tool definition or null if not found
 */
export function getToolDefinition(toolName) {
    const allTools = getAvailableTools();
    return allTools.find(tool => tool.name === toolName) || null;
}

/**
 * Check if a tool exists
 * 
 * @param {string} toolName - Name of the tool
 * @returns {boolean} True if tool exists
 */
export function hasTool(toolName) {
    const implementations = getToolImplementations();
    return toolName in implementations;
}

/**
 * Get tool information
 * 
 * @param {string} toolName - Name of the tool
 * @returns {Promise<Object|null>} Tool information or null if not found
 */
export async function getToolInfo(toolName) {
    // Check if it's a dynamic MCP tool
    const mcpTools = dynamicToolDefinitions;
    const mcpTool = mcpTools.find(t => t.name === toolName);
    
    if (mcpTool) {
        return {
            name: mcpTool.name,
            description: mcpTool.description,
            parameters: mcpTool.inputSchema || {},
            source: 'mcp'
        };
    }
    
    // Check static tools
    const staticToolDef = [LIST_FILES_TOOL].find(t => t.name === toolName);
    if (staticToolDef) {
        return {
            name: staticToolDef.name,
            description: staticToolDef.description,
            parameters: staticToolDef.parameters || {},
            examples: staticToolDef.examples || [],
            source: 'static'
        };
    }
    
    return null;
}

/**
 * Format tool result for sending to AI
 * 
 * @param {string} toolName - Name of the tool
 * @param {any} result - Tool execution result
 * @param {Error} [error] - Error if tool failed
 * @returns {Object} Formatted result
 */
export function formatToolResult(toolName, result, error = null) {
    if (error) {
        return {
            tool_name: toolName,
            success: false,
            error: error.message,
            timestamp: new Date().toISOString()
        };
    }
    
    return {
        tool_name: toolName,
        success: true,
        result: result,
        timestamp: new Date().toISOString()
    };
}

/**
 * Process tool calls from AI response
 * This handles the tool execution flow for MCP
 * 
 * @param {Array} toolCalls - Array of tool calls from AI
 * @returns {Promise<Array>} Array of tool results
 */
export async function processToolCalls(toolCalls) {
    const results = [];
    
    for (const call of toolCalls) {
        const { name, arguments: args } = call;
        
        try {
            const result = await executeTool(name, args);
            results.push(formatToolResult(name, result));
        } catch (error) {
            results.push(formatToolResult(name, null, error));
        }
    }
    
    return results;
}

/**
 * Get MCP server status
 */
export async function getToolSystemStatus() {
    const mcpStatus = await getMCPServerStatus();
    const implementations = getToolImplementations();
    
    return {
        staticTools: Object.keys(staticTools).length,
        dynamicTools: Object.keys(dynamicTools).length,
        totalTools: Object.keys(implementations).length,
        mcpServers: mcpStatus
    };
}

// Export for backward compatibility
export const AVAILABLE_TOOLS = getAvailableTools();

// Auto-initialize on module load
initializeToolRouter().catch(err => {
    toolLog(`Warning: Tool router initialization failed: ${err.message}`);
});