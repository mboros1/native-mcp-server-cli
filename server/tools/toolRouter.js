/**
 * @file toolRouter.js
 * @brief MCP tool router and management
 * @author Native MCP Team
 * @date 2025
 * 
 * Central router for all MCP tools. Handles tool discovery,
 * validation, and execution.
 */

import { listFiles, LIST_FILES_TOOL } from './listFiles.js';
import { 
  mcpReadFile, 
  mcpWriteFile, 
  mcpCreateDirectory, 
  mcpListDirectory,
  mcpMoveFile,
  mcpSearchFiles,
  mcpGetFileInfo,
  MCP_FILESYSTEM_TOOLS 
} from './mcpFilesystem.js';
import { toolLog } from '../lib/logger.js';

/**
 * Registry of all available tools
 * Maps tool name to its implementation function
 */
const toolImplementations = {
  list_files: listFiles,
  // MCP filesystem tools
  mcp_read_file: mcpReadFile,
  mcp_write_file: mcpWriteFile,
  mcp_create_directory: mcpCreateDirectory,
  mcp_list_directory: mcpListDirectory,
  mcp_move_file: mcpMoveFile,
  mcp_search_files: mcpSearchFiles,
  mcp_get_file_info: mcpGetFileInfo
};

/**
 * Tool definitions for MCP protocol
 * This is what gets sent to the AI model
 */
export const AVAILABLE_TOOLS = [
  LIST_FILES_TOOL,
  ...MCP_FILESYSTEM_TOOLS
];

/**
 * Execute a tool by name with given arguments
 * 
 * @param {string} toolName - Name of the tool to execute
 * @param {Object} args - Arguments to pass to the tool
 * @returns {Promise<any>} Tool execution result
 * @throws {Error} If tool not found or execution fails
 */
export async function executeTool(toolName, args) {
  const implementation = toolImplementations[toolName];
  
  if (!implementation) {
    throw new Error(`Unknown tool: ${toolName}`);
  }
  
  try {
    // Log tool execution for debugging (to file, not console!)
    toolLog(`[Tool] Executing ${toolName} with args:`, JSON.stringify(args, null, 2));
    
    // Execute the tool
    const result = await implementation(args);
    
    toolLog(`[Tool] ${toolName} completed successfully`);
    return result;
  } catch (error) {
    toolLog(`[Tool] ${toolName} failed:`, error.message);
    throw error;
  }
}

/**
 * Get tool definition by name
 * 
 * @param {string} toolName - Name of the tool
 * @returns {Object|null} Tool definition or null if not found
 */
export function getToolDefinition(toolName) {
  return AVAILABLE_TOOLS.find(tool => tool.name === toolName) || null;
}

/**
 * Check if a tool exists
 * 
 * @param {string} toolName - Name of the tool
 * @returns {boolean} True if tool exists
 */
export function hasToool(toolName) {
  return toolName in toolImplementations;
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