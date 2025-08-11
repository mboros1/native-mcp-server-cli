import { executeTool, AVAILABLE_TOOLS } from '../tools/toolRouter.js';
import ResponseFormatter from './responseFormatter.js';

/**
 * Message Handlers
 * Individual handlers for different message types
 */

/**
 * Handle /new command - rotate chat history
 */
export async function handleNewCommand(payload, { socket, clientId, chatHistory, log }) {
    const startTime = Date.now();
    log(`Processing /new command for ${clientId} at ${new Date().toISOString()}`);
    
    chatHistory.rotate();
    
    socket.write(ResponseFormatter.chatResponse(
        'Started new conversation. Chat history has been rotated.',
        payload
    ));
    
    const endTime = Date.now();
    log(`Chat history rotated for ${clientId} in ${endTime - startTime}ms`);
}

/**
 * Handle reload request
 */
export async function handleReload(payload, { socket, clientId, chatHistory, log }) {
    log(`Reload request from ${clientId} - reloading chat history from file`);
    const count = chatHistory.load();
    log(`Reloaded chat history: ${count} entries`);
}

/**
 * Handle sync request
 */
export async function handleSync(payload, { socket, clientId, chatHistory, log }) {
    const serverStats = `Server: ${chatHistory.getLength()} entries`;
    socket.write(ResponseFormatter.syncResponse(serverStats, chatHistory.getHistory()));
    log(`Sent sync response to ${clientId}: ${serverStats}`);
}

/**
 * Handle tool list request
 */
export async function handleToolList(payload, { socket, clientId, log }) {
    log(`Tool list request from ${clientId}`);
    socket.write(ResponseFormatter.toolListResponse(AVAILABLE_TOOLS));
    log(`Sent ${AVAILABLE_TOOLS.length} tool definitions to ${clientId}`);
}

/**
 * Handle tool execution request
 */
export async function handleToolExecute(payload, { socket, clientId, log }) {
    const { tool_name, arguments: args } = payload;
    log(`Tool execution request from ${clientId}: ${tool_name}`);
    
    try {
        const result = await executeTool(tool_name, args);
        socket.write(ResponseFormatter.toolExecutionResult(tool_name, true, result));
        log(`Tool ${tool_name} executed successfully for ${clientId}`);
    } catch (error) {
        socket.write(ResponseFormatter.toolExecutionResult(tool_name, false, error.message));
        log(`Tool ${tool_name} failed for ${clientId}: ${error.message}`);
    }
}

/**
 * Handle reset/interrupt request
 */
export async function handleReset(payload, { socket, clientId, activeRequests, log }) {
    log(`Reset/interrupt request from ${clientId}`);
    
    if (activeRequests && activeRequests.has(clientId)) {
        const controller = activeRequests.get(clientId);
        controller.abort();
        activeRequests.delete(clientId);
        log(`Aborted active request for ${clientId}`);
    }
}

/**
 * Execute tool calls from LLM response
 */
export async function executeToolCalls(toolCalls, socket, log) {
    const toolResults = [];
    
    for (const toolCall of toolCalls) {
        const { function: func } = toolCall;
        log(`Executing tool: ${func.name}`);
        
        try {
            const args = JSON.parse(func.arguments);
            
            // Send tool call event to UI
            socket.write(ResponseFormatter.toolCallEvent(func.name, args));
            
            const result = await executeTool(func.name, args);
            
            // Send tool result preview to UI
            let preview = '';
            if (result.entries && Array.isArray(result.entries)) {
                preview = result.entries.slice(0, 20).map(e => 
                    `  ${e.type === 'dir' ? '📁' : '📄'} ${e.path}`
                ).join('\n');
                if (result.entries.length > 20) {
                    preview += `\n  ... and ${result.entries.length - 20} more`;
                }
            } else {
                preview = JSON.stringify(result, null, 2).split('\n').slice(0, 20).join('\n');
            }
            
            socket.write(ResponseFormatter.toolResultPreview(
                func.name, 
                preview, 
                result.entries ? result.entries.length : 0
            ));
            
            toolResults.push({
                tool_call_id: toolCall.id,
                role: 'tool',
                name: func.name,
                content: JSON.stringify(result)
            });
            
            log(`Tool ${func.name} executed successfully`);
        } catch (error) {
            socket.write(ResponseFormatter.toolErrorEvent(func.name, error.message));
            
            toolResults.push({
                tool_call_id: toolCall.id,
                role: 'tool',
                name: func.name,
                content: JSON.stringify({ error: error.message })
            });
            
            log(`Tool ${func.name} failed: ${error.message}`);
        }
    }
    
    return toolResults;
}