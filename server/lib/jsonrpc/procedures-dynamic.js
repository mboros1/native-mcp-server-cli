/**
 * Dynamic JSON-RPC 2.0 Procedure Handlers with MCP Tool Support
 * 
 * This version integrates with the dynamic MCP tool system
 */

import {
    HistoryProcedures,
    SystemProcedures
} from './procedures.js';
import { JsonRpcErrorCodes } from './JsonRpcServer.js';
import { log } from '../logger.js';
import { 
    getAvailableTools, 
    executeTool as executeDynamicTool,
    getToolInfo,
    formatToolResult
} from '../../tools/toolRouter-dynamic.js';
import { AgentOrchestrator } from '../agents/index.js';
import { ReactAgent } from '../agents/strategies/ReactAgent.js';

/**
 * Dynamic Chat procedure handlers that use dynamic tools
 */
export class DynamicChatProcedures {
    constructor(apiManager, chatHistory) {
        this.apiManager = apiManager;
        this.chatHistory = chatHistory;
        this.activeSessions = new Map();
        
        // Initialize agent orchestrator
        this.agentOrchestrator = new AgentOrchestrator({
            apiManager: this.apiManager,
            toolExecutor: executeDynamicTool,
            verbose: process.env.DEBUG_AGENTS === 'true'
        });
        
        // Register agent strategies
        this.agentOrchestrator.registerStrategy('react', new ReactAgent({
            maxIterations: 10,
            verbose: process.env.DEBUG_AGENTS === 'true'
        }));
        
        // Track whether to use agent mode
        this.useAgentMode = process.env.USE_AGENT_MODE !== 'false';
    }
    
    /**
     * chat.send - Send a chat message with dynamic tool support
     */
    async send(params, context = {}) {
        const { content, model = 'o3', timeout = 60000, reasoning_effort } = params;
        
        if (!content) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: 'Missing required parameter: content'
            };
        }
        
        log(`Processing chat.send: model=${model}, content=${content.substring(0, 50)}...`);
        
        try {
            // Add user message to history
            this.chatHistory.add('user', content);
            log(`Added user message to in-memory history (${this.chatHistory.getMessageCount()} total)`);
            
            // Get message history for API
            const history = this.chatHistory.getApiMessages();
            
            // Prepare tools if model supports them
            let tools = null;
            if (this.apiManager.getModelInfo(model)?.supportsTools) {
                const toolDefinitions = getAvailableTools();
                tools = toolDefinitions.map(tool => ({
                    type: 'function',
                    function: {
                        name: tool.name,
                        description: tool.description,
                        parameters: tool.parameters
                    }
                }));
                log(`Prepared ${tools.length} tools for model ${model}`);
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
            
            // Check if we should use agent mode for tool handling
            let finalContent = response.content;
            
            if (response.toolCalls && response.toolCalls.length > 0) {
                log(`Model requested ${response.toolCalls.length} tool call(s)`);
                
                if (this.useAgentMode && this.shouldUseAgent(response)) {
                    // Use agent orchestrator for complex tool interactions
                    log('Using agent orchestrator for tool execution');
                    
                    const agentResult = await this.agentOrchestrator.run(content, {
                        model,
                        strategy: 'react',
                        context: {
                            tools: getAvailableTools(),
                            initialResponse: response,
                            history: history
                        }
                    });
                    
                    if (agentResult.success) {
                        finalContent = agentResult.result.answer || agentResult.result;
                        log(`Agent completed successfully after ${agentResult.stats.iterations} iterations`);
                    } else {
                        log(`Agent failed: ${agentResult.error}`);
                        // Fall back to simple tool execution
                        finalContent = await this.simpleToolExecution(response.toolCalls, response.content);
                    }
                } else {
                    // Simple single-pass tool execution
                    finalContent = await this.simpleToolExecution(response.toolCalls, response.content);
                }
            }
            
            // Add assistant response to history
            this.chatHistory.add('assistant', finalContent);
            log(`Added assistant response to in-memory history`);
            
            return {
                reply: finalContent,  // C++ client expects 'reply' not 'content'
                timestamp: Date.now(),
                model: response.model,
                usage: response.usage
            };
            
        } catch (err) {
            log(`Error processing chat.send: ${err.message}`);
            
            // Map errors to JSON-RPC error codes
            if (err.message?.includes('API error')) {
                throw {
                    code: JsonRpcErrorCodes.API_ERROR,
                    message: err.message
                };
            }
            
            throw {
                code: JsonRpcErrorCodes.INTERNAL_ERROR,
                message: err.message || 'Chat processing failed'
            };
        }
    }
    
    /**
     * chat.retry - Retry the last message
     */
    async retry(params, context = {}) {
        const messages = this.chatHistory.getMessages();
        
        // Find the last user message
        let lastUserMessage = null;
        for (let i = messages.length - 1; i >= 0; i--) {
            if (messages[i].role === 'user') {
                lastUserMessage = messages[i].content;
                break;
            }
        }
        
        if (!lastUserMessage) {
            throw {
                code: JsonRpcErrorCodes.INVALID_REQUEST,
                message: 'No previous message to retry'
            };
        }
        
        log('Retrying last message');
        
        // Use the send method with the last message
        return this.send({
            ...params,
            content: lastUserMessage
        }, context);
    }
    
    /**
     * chat.clear - Clear conversation history
     */
    async clear() {
        this.chatHistory.clear();
        log('Cleared conversation history');
        
        return {
            success: true,
            message_count: 0
        };
    }
    
    /**
     * Determine if we should use the agent for this response
     */
    shouldUseAgent(response) {
        // ALWAYS use agent if there are ANY tool calls when agent mode is enabled
        // The agent will handle:
        // 1. Single tool execution that completes immediately
        // 2. Multi-step tool chains
        // 3. Proper termination when the model stops requesting tools
        
        // This is the standard pattern used by OpenAI and LangChain:
        // - If model returns tool calls -> continue agent loop
        // - If model returns only text -> agent is done
        
        return response.toolCalls && response.toolCalls.length > 0;
    }
    
    /**
     * Simple single-pass tool execution (non-agent)
     */
    async simpleToolExecution(toolCalls, initialContent) {
        const toolResults = [];
        
        for (const toolCall of toolCalls) {
            const { function: func } = toolCall;
            log(`Executing tool: ${func.name}`);
            
            try {
                const args = JSON.parse(func.arguments);
                const result = await executeDynamicTool(func.name, args);
                const formattedResult = formatToolResult(func.name, result);
                toolResults.push(formattedResult);
                log(`Tool ${func.name} executed successfully`);
            } catch (err) {
                const errorResult = `Error executing ${func.name}: ${err.message}`;
                toolResults.push(errorResult);
                log(`Tool ${func.name} failed: ${err.message}`);
            }
        }
        
        // Format tool results for display
        const toolResultsText = toolResults.map(r => 
            typeof r === 'object' ? JSON.stringify(r, null, 2) : r
        ).join('\n\n');
        
        // Add tool results to the response
        return `${initialContent}\n\n**Tool Results:**\n${toolResultsText}`;
    }
}

/**
 * Dynamic Tool procedure handlers
 */
export class DynamicToolProcedures {
    /**
     * tools.list - List all available tools (static + dynamic)
     * 
     * @returns {Object} - {tools: Array<string>}
     */
    async list() {
        const toolDefinitions = getAvailableTools();
        // Extract just the tool names for the response
        const tools = toolDefinitions.map(t => t.name);
        log(`[Tool List] Returning ${tools.length} available tools`);
        return { tools };
    }
    
    /**
     * tool.execute - Execute a tool (static or dynamic)
     * 
     * @param {Object} params - {name: string, arguments: Object}
     * @returns {Object} - Tool execution result
     */
    async execute(params) {
        const { name, arguments: args } = params;
        
        if (!name) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: 'Missing required parameter: name'
            };
        }
        
        log(`[Tool Execute] Starting execution of tool: ${name}`);
        log(`[Tool Execute] Arguments: ${JSON.stringify(args)}`);
        
        try {
            const result = await executeDynamicTool(name, args || {});
            log(`[Tool Execute] Tool ${name} completed successfully`);
            log(`[Tool Execute] Result: ${JSON.stringify(result).substring(0, 200)}...`);
            return result;
        } catch (error) {
            log(`[Tool Execute] Tool ${name} failed: ${error.message}`);
            throw {
                code: JsonRpcErrorCodes.INTERNAL_ERROR,
                message: `Tool execution failed: ${error.message}`
            };
        }
    }
    
    /**
     * tool.info - Get information about a specific tool
     * 
     * @param {Object} params - {name: string}
     * @returns {Object} - Tool information
     */
    async info(params) {
        const { name } = params;
        
        if (!name) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: 'Missing required parameter: name'
            };
        }
        
        log(`[Tool Info] Getting info for tool: ${name}`);
        
        try {
            const info = await getToolInfo(name);
            if (!info) {
                throw new Error(`Unknown tool: ${name}`);
            }
            return info;
        } catch (error) {
            throw {
                code: JsonRpcErrorCodes.INVALID_PARAMS,
                message: error.message
            };
        }
    }
}

/**
 * Register all procedures with the JSON-RPC server (Dynamic version)
 * 
 * @param {JsonRpcServer} server - JSON-RPC server instance
 * @param {Object} dependencies - Service dependencies
 */
export function registerAllProceduresDynamic(server, dependencies) {
    const { apiManager, chatHistory } = dependencies;
    
    // Initialize procedure handlers
    const chat = new DynamicChatProcedures(apiManager, chatHistory); // Use dynamic chat with tool support
    const tools = new DynamicToolProcedures(); // Use dynamic version
    const history = new HistoryProcedures(chatHistory);
    const system = new SystemProcedures(server);
    
    // Register chat procedures
    server.registerMethod('chat.send', chat.send.bind(chat));
    server.registerMethod('chat.retry', chat.retry.bind(chat));
    server.registerMethod('chat.clear', chat.clear.bind(chat));
    
    // Register tool procedures (dynamic)
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
    
    log('Registered all JSON-RPC procedures (with dynamic tools)');
    
    return {
        chat,
        tools,
        history,
        system
    };
}