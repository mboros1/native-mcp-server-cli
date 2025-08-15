/**
 * JSON-RPC Procedure Implementations
 * 
 * Implements the procedures defined in the C++ jsonrpc_procedures.hpp
 * Each procedure matches the expected params and result types.
 */

import { executeTool, AVAILABLE_TOOLS, formatToolResult } from '../../tools/toolRouter.js';
import { log } from '../logger.js';
import { JsonRpcErrorCodes } from './JsonRpcServer.js';

/**
 * Chat procedure handler registry
 * Manages chat sessions and API interactions
 */
export class ChatProcedures {
    constructor(apiManager, chatHistory) {
        this.apiManager = apiManager;
        this.chatHistory = chatHistory;
        this.activeSessions = new Map();
    }
    
    /**
     * chat.send - Send a chat message and get response
     * 
     * @param {Object} params
     * @param {string} params.content - Message content
     * @param {string} [params.model] - Model to use (kimi, o3, etc)
     * @param {number} [params.timeout] - Timeout in ms
     * @param {string} [params.reasoning_effort] - Reasoning effort level
     * @returns {Promise<Object>} - {reply: string, timestamp: number, streaming: boolean}
     */
    async send(params, context) {
        const { content, model = 'kimi', timeout = 300000, reasoning_effort = 'medium' } = params;
        
        if (!content) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: 'Missing required parameter: content'
            };
        }
        
        log(`Processing chat.send: model=${model}, content=${content.slice(0, 50)}...`);
        
        try {
            // Add to chat history
            this.chatHistory.add('user', content);
            
            // Get current history for context
            const history = this.chatHistory.getApiMessages();
            
            // Prepare tools if model supports them
            let tools = null;
            if (this.apiManager.getModelInfo(model)?.supportsTools) {
                tools = AVAILABLE_TOOLS.map(tool => ({
                    type: 'function',
                    function: {
                        name: tool.name,
                        description: tool.description,
                        parameters: tool.parameters
                    }
                }));
            }
            
            // Get API response with full history and tools
            const response = await this.apiManager.sendMessage(
                content,
                model,
                reasoning_effort,
                timeout,
                context.abortSignal,
                history.slice(0, -1),  // Exclude the last message we just added
                tools
            );
            
            // Handle tool calls if present
            let finalContent = response.content;
            
            if (response.toolCalls && response.toolCalls.length > 0) {
                log(`Model requested ${response.toolCalls.length} tool call(s)`);
                
                // Execute tool calls
                const toolResults = [];
                for (const toolCall of response.toolCalls) {
                    const { function: func } = toolCall;
                    log(`Executing tool: ${func.name}`);
                    
                    try {
                        const args = JSON.parse(func.arguments);
                        const result = await executeTool(func.name, args);
                        const formattedResult = formatToolResult(func.name, result);
                        toolResults.push(formattedResult);
                        log(`Tool ${func.name} executed successfully`);
                    } catch (err) {
                        const errorResult = `Error executing ${func.name}: ${err.message}`;
                        toolResults.push(errorResult);
                        log(`Tool ${func.name} failed: ${err.message}`);
                    }
                }
                
                // Combine tool results with response
                if (toolResults.length > 0) {
                    const toolOutput = toolResults.join('\n\n');
                    finalContent = response.content ? 
                        `${response.content}\n\n${toolOutput}` : 
                        toolOutput;
                }
            }
            
            // Add assistant response to history
            this.chatHistory.add('assistant', finalContent);
            
            // Return in expected format
            return {
                reply: finalContent,
                timestamp: Math.floor(Date.now() / 1000),
                streaming: false
            };
            
        } catch (err) {
            // Handle specific error types
            if (err.name === 'AbortError') {
                throw {
                    code: JsonRpcErrorCodes.CANCELLED,
                    message: 'Request was cancelled'
                };
            }
            
            if (err.timeout) {
                throw {
                    code: JsonRpcErrorCodes.TIMEOUT,
                    message: `Request timed out after ${timeout}ms`
                };
            }
            
            // API errors
            if (err.response) {
                throw {
                    code: JsonRpcErrorCodes.MODEL_ERROR,
                    message: `API error: ${err.response.status}`,
                    data: err.response.data
                };
            }
            
            throw err;
        }
    }
    
    /**
     * chat.retry - Retry the last message
     * 
     * @param {Object} params
     * @param {string} [params.content] - Optional new content
     * @returns {Promise<Object>} - {reply: string, timestamp: number, streaming: boolean}
     */
    async retry(params, context) {
        const { content } = params;
        
        // Get last user message if no content provided
        const messageToRetry = content || this.chatHistory.getLastUserMessage();
        
        if (!messageToRetry) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: 'No message to retry'
            };
        }
        
        // Remove last exchange if retrying
        if (!content) {
            this.chatHistory.popLastExchange();
        }
        
        // Process as new message
        return this.send({
            content: messageToRetry,
            ...params
        }, context);
    }
    
    /**
     * chat.clear - Clear chat history
     * 
     * @returns {Promise<Object>} - {success: boolean, timestamp: number}
     */
    async clear() {
        this.chatHistory.clear();
        
        return {
            success: true,
            timestamp: Math.floor(Date.now() / 1000)
        };
    }
}

/**
 * Tool procedure handlers
 */
export class ToolProcedures {
    /**
     * tools.list - List available tools
     * 
     * @returns {Promise<Object>} - {tools: string[], timestamp: number}
     */
    async list() {
        const toolNames = AVAILABLE_TOOLS.map(tool => tool.name);
        
        return {
            tools: toolNames,
            timestamp: Math.floor(Date.now() / 1000)
        };
    }
    
    /**
     * tool.execute - Execute a tool
     * 
     * @param {Object} params
     * @param {string} params.name - Tool name
     * @param {Object} [params.arguments] - Tool arguments
     * @returns {Promise<Object>} - {success: boolean, output: string, error?: string, execution_time_ms: number}
     */
    async execute(params) {
        const { name, arguments: args = {} } = params;
        
        if (!name) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: 'Missing required parameter: name'
            };
        }
        
        const startTime = Date.now();
        
        try {
            const result = await executeTool(name, args);
            
            return {
                success: true,
                output: typeof result === 'string' ? result : JSON.stringify(result),
                execution_time_ms: Date.now() - startTime
            };
            
        } catch (err) {
            return {
                success: false,
                output: '',
                error: err.message,
                execution_time_ms: Date.now() - startTime
            };
        }
    }
    
    /**
     * tool.info - Get tool information
     * 
     * @param {Object} params
     * @param {string} params.name - Tool name
     * @returns {Promise<Object>} - Tool definition
     */
    async info(params) {
        const { name } = params;
        
        if (!name) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: 'Missing required parameter: name'
            };
        }
        
        const tool = AVAILABLE_TOOLS.find(t => t.name === name);
        
        if (!tool) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: `Unknown tool: ${name}`
            };
        }
        
        return {
            name: tool.name,
            description: tool.description,
            parameters: tool.parameters,
            examples: tool.examples || []
        };
    }
}

/**
 * History procedure handlers
 * Note: C++ client manages file persistence, server just coordinates
 */
export class HistoryProcedures {
    constructor(chatHistory) {
        this.chatHistory = chatHistory;
    }
    
    /**
     * history.reload - Reload history from C++ file
     * 
     * @returns {Promise<Object>} - {success: boolean, message_count: number}
     */
    async reload() {
        const success = this.chatHistory.reload();
        
        return {
            success,
            message_count: this.chatHistory.getMessageCount()
        };
    }
    
    /**
     * history.sync - Get sync stats for C++ client
     * 
     * @returns {Promise<Object>} - Server history state
     */
    async sync() {
        const stats = this.chatHistory.getSyncStats();
        const messages = this.chatHistory.getMessages();
        
        return {
            server_stats: `Server: ${stats.message_count} entries`,
            server_history: messages,
            timestamp: Date.now()
        };
    }
    
    /**
     * history.rotate - Clear memory for new conversation
     * Note: C++ handles actual file rotation
     * 
     * @returns {Promise<Object>} - {success: boolean}
     */
    async rotate() {
        this.chatHistory.clear();
        
        return {
            success: true,
            message: 'Started new conversation. Chat history has been rotated.'
        };
    }
}

/**
 * System/RPC procedure handlers
 */
export class SystemProcedures {
    constructor(server) {
        this.server = server;
        this.sessionId = Math.random().toString(36).substr(2, 9);
        this.startTime = Date.now();
    }
    
    /**
     * rpc.hello - Initial handshake
     * 
     * @returns {Promise<Object>} - Server capabilities and info
     */
    async hello() {
        return {
            version: '2.0',
            serverVersion: '1.0.0',
            capabilities: [
                'chat',
                'tools',
                'history',
                'streaming',
                'cancellation'
            ],
            sessionId: this.sessionId
        };
    }
    
    /**
     * request.cancel - Cancel a pending request
     * 
     * @param {Object} params
     * @param {number} [params.request_id] - Request ID to cancel
     * @returns {Promise<Object>} - {success: boolean, was_running: boolean}
     */
    async cancel(params = {}) {
        const { request_id } = params;
        
        if (!request_id) {
            // Cancel all active requests
            const activeCount = this.server.activeRequests.size;
            this.server.activeRequests.forEach((_, id) => {
                this.server.cancelRequest(id);
            });
            
            return {
                success: true,
                was_running: activeCount > 0,
                cancelled_count: activeCount
            };
        }
        
        // Cancel specific request
        const wasRunning = this.server.getActiveRequest(request_id) !== null;
        const success = this.server.cancelRequest(request_id);
        
        return {
            success,
            was_running: wasRunning
        };
    }
    
    /**
     * system.status - Get system status
     * 
     * @returns {Promise<Object>} - System status info
     */
    async status() {
        const stats = this.server.getStats();
        const uptime = Math.floor((Date.now() - this.startTime) / 1000);
        
        return {
            status: 'online',
            uptime_seconds: uptime,
            session_id: this.sessionId,
            stats
        };
    }
}

/**
 * Register all procedures with the JSON-RPC server
 * 
 * @param {import('./JsonRpcServer.js').JsonRpcServer} server - JSON-RPC server instance
 * @param {Object} dependencies - Service dependencies
 */
export function registerAllProcedures(server, dependencies) {
    const { apiManager, chatHistory } = dependencies;
    
    // Initialize procedure handlers
    const chat = new ChatProcedures(apiManager, chatHistory);
    const tools = new ToolProcedures();
    const history = new HistoryProcedures(chatHistory);
    const system = new SystemProcedures(server);
    
    // Register chat procedures
    server.registerMethod('chat.send', chat.send.bind(chat));
    server.registerMethod('chat.retry', chat.retry.bind(chat));
    server.registerMethod('chat.clear', chat.clear.bind(chat));
    
    // Register tool procedures
    server.registerMethod('tools.list', tools.list.bind(tools));
    server.registerMethod('tool.execute', tools.execute.bind(tools));
    server.registerMethod('tool.info', tools.info.bind(tools));
    
    // Register history procedures
    server.registerMethod('history.reload', history.reload.bind(history));
    server.registerMethod('history.sync', history.sync.bind(history));
    server.registerMethod('history.rotate', history.rotate.bind(history));
    
    // Register system procedures
    server.registerMethod('rpc.hello', system.hello.bind(system));
    server.registerMethod('request.cancel', system.cancel.bind(system));
    server.registerMethod('system.status', system.status.bind(system));
    
    log('Registered all JSON-RPC procedures');
    
    return {
        chat,
        tools,
        history,
        system
    };
}