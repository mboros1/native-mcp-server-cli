/**
 * MCP Filesystem Server Integration
 * Wraps the external MCP filesystem server for use with our bridge
 */

import MCPClient from '../lib/mcpClient.js';
import { toolLog } from '../lib/logger.js';

// Singleton MCP client for filesystem server
let mcpClient = null;

/**
 * Initialize connection to MCP filesystem server
 */
export async function initializeMCPFilesystem() {
    if (mcpClient && mcpClient.connected) {
        return mcpClient;
    }

    mcpClient = new MCPClient();
    
    // The filesystem server is already running, we'll spawn a new one
    // with our working directory
    const workingDir = process.cwd();
    
    try {
        // Connect to the filesystem MCP server
        // Using npx to run the server with our working directory
        await mcpClient.connectToProcess('npx', [
            '@modelcontextprotocol/server-filesystem@latest',
            workingDir
        ]);
        
        toolLog(`Connected to MCP filesystem server with working directory: ${workingDir}`);
        
        // Get available tools from the server
        const tools = await mcpClient.listTools();
        toolLog(`Available MCP filesystem tools: ${JSON.stringify(tools, null, 2)}`);
        
        return mcpClient;
    } catch (err) {
        toolLog(`Failed to connect to MCP filesystem server: ${err.message}`);
        throw err;
    }
}

/**
 * Read file using MCP filesystem server
 */
export async function mcpReadFile(args) {
    const client = await initializeMCPFilesystem();
    
    try {
        const result = await client.executeTool('read_file', {
            path: args.path || args.file_path
        });
        
        return result;
    } catch (err) {
        toolLog(`MCP read_file error: ${err.message}`);
        throw err;
    }
}

/**
 * Write file using MCP filesystem server
 */
export async function mcpWriteFile(args) {
    const client = await initializeMCPFilesystem();
    
    try {
        const result = await client.executeTool('write_file', {
            path: args.path || args.file_path,
            content: args.content
        });
        
        return result;
    } catch (err) {
        toolLog(`MCP write_file error: ${err.message}`);
        throw err;
    }
}

/**
 * Create directory using MCP filesystem server
 */
export async function mcpCreateDirectory(args) {
    const client = await initializeMCPFilesystem();
    
    try {
        const result = await client.executeTool('create_directory', {
            path: args.path
        });
        
        return result;
    } catch (err) {
        toolLog(`MCP create_directory error: ${err.message}`);
        throw err;
    }
}

/**
 * List directory using MCP filesystem server
 */
export async function mcpListDirectory(args) {
    const client = await initializeMCPFilesystem();
    
    try {
        const result = await client.executeTool('list_directory', {
            path: args.path || '.'
        });
        
        return result;
    } catch (err) {
        toolLog(`MCP list_directory error: ${err.message}`);
        throw err;
    }
}

/**
 * Move/rename file using MCP filesystem server
 */
export async function mcpMoveFile(args) {
    const client = await initializeMCPFilesystem();
    
    try {
        const result = await client.executeTool('move_file', {
            source: args.source,
            destination: args.destination
        });
        
        return result;
    } catch (err) {
        toolLog(`MCP move_file error: ${err.message}`);
        throw err;
    }
}

/**
 * Search files using MCP filesystem server
 */
export async function mcpSearchFiles(args) {
    const client = await initializeMCPFilesystem();
    
    try {
        const result = await client.executeTool('search_files', {
            path: args.path || '.',
            pattern: args.pattern,
            excludePatterns: args.excludePatterns
        });
        
        return result;
    } catch (err) {
        toolLog(`MCP search_files error: ${err.message}`);
        throw err;
    }
}

/**
 * Get file info using MCP filesystem server
 */
export async function mcpGetFileInfo(args) {
    const client = await initializeMCPFilesystem();
    
    try {
        const result = await client.executeTool('get_file_info', {
            path: args.path
        });
        
        return result;
    } catch (err) {
        toolLog(`MCP get_file_info error: ${err.message}`);
        throw err;
    }
}

// Tool definitions for MCP filesystem tools
export const MCP_FILESYSTEM_TOOLS = [
    {
        name: 'mcp_read_file',
        description: 'Read a file from the filesystem',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'Path to the file to read'
                }
            },
            required: ['path']
        }
    },
    {
        name: 'mcp_write_file',
        description: 'Write content to a file',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'Path to the file to write'
                },
                content: {
                    type: 'string',
                    description: 'Content to write to the file'
                }
            },
            required: ['path', 'content']
        }
    },
    {
        name: 'mcp_create_directory',
        description: 'Create a new directory',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'Path of the directory to create'
                }
            },
            required: ['path']
        }
    },
    {
        name: 'mcp_list_directory',
        description: 'List contents of a directory',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'Path to the directory to list (default: current directory)'
                }
            }
        }
    },
    {
        name: 'mcp_move_file',
        description: 'Move or rename a file',
        inputSchema: {
            type: 'object',
            properties: {
                source: {
                    type: 'string',
                    description: 'Source file path'
                },
                destination: {
                    type: 'string',
                    description: 'Destination file path'
                }
            },
            required: ['source', 'destination']
        }
    },
    {
        name: 'mcp_search_files',
        description: 'Search for files matching a pattern',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'Directory to search in'
                },
                pattern: {
                    type: 'string',
                    description: 'Search pattern (glob or regex)'
                },
                excludePatterns: {
                    type: 'array',
                    items: { type: 'string' },
                    description: 'Patterns to exclude from search'
                }
            },
            required: ['pattern']
        }
    },
    {
        name: 'mcp_get_file_info',
        description: 'Get detailed information about a file',
        inputSchema: {
            type: 'object',
            properties: {
                path: {
                    type: 'string',
                    description: 'Path to the file'
                }
            },
            required: ['path']
        }
    }
];