import { AVAILABLE_TOOLS } from '../tools/toolRouter.js';
import ResponseFormatter from './responseFormatter.js';
import { executeToolCalls } from './messageHandlers.js';

/**
 * Chat Processor
 * Handles chat message processing with AI models
 */
class ChatProcessor {
    constructor(apiManager, chatHistory, logger) {
        this.apiManager = apiManager;
        this.chatHistory = chatHistory;
        this.log = logger;
        this.activeRequests = new Map();
    }

    /**
     * Process a chat message
     * @param {Object} socket - Client socket
     * @param {string} clientId - Client identifier
     * @param {string} message - User message
     * @param {number|null} timeout - Timeout in ms
     * @param {string} modelKey - Model to use
     * @param {string|null} reasoningEffort - Reasoning effort level
     */
    async processMessage(socket, clientId, message, timeout = null, modelKey = 'kimi', reasoningEffort = null) {
        const startTime = Date.now();
        const requestTimeout = timeout || 120000; // Default 2 minutes
        
        this.log(`Processing chat message from ${clientId} using model ${modelKey}: ${message.slice(0, 50)}...`);
        
        // Add user message to history
        this.chatHistory.addMessage('user', message);
        
        // Prepare messages for API
        const messages = this.prepareMessages();
        
        // Create abort controller for cancellation
        const controller = new AbortController();
        this.activeRequests.set(clientId, controller);
        
        // Set up timeout
        const timeoutId = setTimeout(() => {
            controller.abort();
            this.activeRequests.delete(clientId);
        }, requestTimeout);
        
        try {
            // Get model configuration
            const model = this.apiManager.getModel(modelKey);
            if (!model) {
                throw new Error(`Unknown model: ${modelKey}`);
            }
            
            // Prepare tools if supported
            const tools = model.supportsTools ? AVAILABLE_TOOLS : null;
            
            // Create request
            const requestData = this.apiManager.createRequest(
                modelKey, 
                messages, 
                { reasoning_effort: reasoningEffort },
                tools
            );
            
            // Make API call
            const response = await this.apiManager.callAPI(modelKey, requestData);
            clearTimeout(timeoutId);
            
            // Process response based on model type
            const result = await this.processModelResponse(
                response, 
                modelKey, 
                socket, 
                clientId, 
                messages,
                reasoningEffort
            );
            
            // Send final response
            socket.write(ResponseFormatter.chatResponse(result.content));
            
            // Add to history
            this.chatHistory.addMessage('assistant', result.content, result.toolCalls);
            
            const endTime = Date.now();
            this.log(`Completed chat processing for ${clientId} in ${endTime - startTime}ms`);
            
        } catch (error) {
            clearTimeout(timeoutId);
            this.handleError(error, socket, clientId, message, requestTimeout);
        } finally {
            this.activeRequests.delete(clientId);
        }
    }

    /**
     * Process model-specific response
     */
    async processModelResponse(response, modelKey, socket, clientId, messages, reasoningEffort) {
        if (modelKey === 'o3') {
            // O3 model response
            return {
                content: response.output,
                toolCalls: null
            };
        }
        
        // OpenAI-style response (Kimi, etc)
        const choice = response.choices[0];
        
        // Check for tool calls
        if (choice.message.tool_calls && choice.message.tool_calls.length > 0) {
            const toolCalls = choice.message.tool_calls;
            this.log(`Model requested ${toolCalls.length} tool call(s)`);
            
            // Execute tools
            const toolResults = await executeToolCalls(toolCalls, socket, this.log);
            
            // Add tool calls to history
            this.chatHistory.addMessage('assistant', null, toolCalls);
            
            // Add tool results to messages
            const followUpMessages = [
                ...messages,
                { role: 'assistant', content: null, tool_calls: toolCalls },
                ...toolResults
            ];
            
            // Make follow-up API call with tool results
            const followUpRequest = this.apiManager.createRequest(
                modelKey,
                followUpMessages,
                { reasoning_effort: reasoningEffort }
            );
            
            const followUpResponse = await this.apiManager.callAPI(modelKey, followUpRequest);
            
            return {
                content: followUpResponse.choices[0].message.content,
                toolCalls: toolCalls
            };
        }
        
        // Check if tools were available but not used
        const model = this.apiManager.getModel(modelKey);
        if (model.supportsTools && AVAILABLE_TOOLS.length > 0) {
            socket.write(ResponseFormatter.toolInfoMessage(
                `AI response without tools (${AVAILABLE_TOOLS.length} available)`
            ));
        }
        
        return {
            content: choice.message.content,
            toolCalls: null
        };
    }

    /**
     * Prepare messages for API call
     */
    prepareMessages() {
        const history = this.chatHistory.getHistory();
        
        // System prompt
        const systemPrompt = {
            role: 'system',
            content: 'You are a helpful AI assistant. You can use available tools when needed to provide accurate information.'
        };
        
        return [systemPrompt, ...history];
    }

    /**
     * Handle errors during processing
     */
    handleError(error, socket, clientId, originalMessage, timeout) {
        if (error.name === 'AbortError' || error.code === 'ECONNABORTED') {
            this.log(`Request timeout for ${clientId} after ${timeout}ms`);
            const canRetry = timeout < 600000; // Can retry if under 10 minutes
            socket.write(ResponseFormatter.errorResponse(
                `Request timed out after ${timeout / 1000} seconds. ${canRetry ? 'You can retry with /retry command.' : 'Maximum timeout reached.'}`,
                canRetry,
                originalMessage
            ));
        } else {
            this.log(`API error for ${clientId}: ${error.message}`);
            socket.write(ResponseFormatter.errorResponse(
                `API Error: ${error.message}`
            ));
        }
        
        // Remove the failed user message from history
        this.chatHistory.removeLastIfRole('user');
    }

    /**
     * Handle retry request
     */
    async handleRetry(socket, clientId, originalMessage, modelKey = 'kimi', reasoningEffort = null) {
        // Check for consecutive retries
        const lastRetry = this.lastRetryTime?.get(clientId);
        if (lastRetry && Date.now() - lastRetry < 1000) {
            socket.write(ResponseFormatter.errorResponse('Please wait before retrying'));
            return;
        }
        
        if (!this.lastRetryTime) {
            this.lastRetryTime = new Map();
        }
        this.lastRetryTime.set(clientId, Date.now());
        
        this.log(`Retry request from ${clientId} for message: ${originalMessage.slice(0, 50)}...`);
        
        // Remove last user message if it exists
        this.chatHistory.removeLastIfRole('user');
        
        // Process with increased timeout
        await this.processMessage(socket, clientId, originalMessage, 600000, modelKey, reasoningEffort);
    }

    /**
     * Cancel active request
     */
    cancelRequest(clientId) {
        if (this.activeRequests.has(clientId)) {
            const controller = this.activeRequests.get(clientId);
            controller.abort();
            this.activeRequests.delete(clientId);
            return true;
        }
        return false;
    }
}

export default ChatProcessor;