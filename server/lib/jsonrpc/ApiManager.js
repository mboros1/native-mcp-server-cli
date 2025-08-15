// @ts-check
/**
 * API Manager for JSON-RPC Server
 * 
 * Manages API clients and model interactions
 */

/** @typedef {import('../../types').ApiResponse} ApiResponse */
/** @typedef {import('../../types').ChatMessage} ChatMessage */
/** @typedef {import('../../types').Tool} Tool */
/** @typedef {import('../../types').ToolCall} ToolCall */

import axios from 'axios';
import { log } from '../logger.js';

/**
 * Model configuration registry
 */
const MODEL_REGISTRY = Object.freeze({
    kimi: {
        id: 'kimi-k2',
        createClient: (apiKey) => axios.create({
            baseURL: 'https://kimi-k2.ai/api',
            timeout: 5 * 60000,
            headers: {
                'Content-Type': 'application/json',
                Authorization: `Bearer ${apiKey}`,
            },
        }),
        endpoint: '/v1/chat/completions',
        extraParams: { temperature: 0.3, max_tokens: 10 * 1024 },
        supportsTools: true,
        formatRequest: (messages, params, reasoning_effort, tools = null) => ({
            model: 'kimi-k2',
            messages,
            ...(tools && tools.length > 0 ? { tools } : {}),
            ...params
        })
    },
    o3: {
        id: 'o3',
        createClient: (apiKey) => axios.create({
            baseURL: 'https://api.openai.com',
            timeout: 5 * 60000,
            headers: {
                'Content-Type': 'application/json',
                Authorization: `Bearer ${apiKey}`,
            },
        }),
        endpoint: '/v1/responses',
        extraParams: {
            max_output_tokens: 10 * 1024,
        },
        supportsTools: false,
        formatRequest: (messages, params, reasoning_effort = 'medium') => ({
            model: 'o3',
            messages,
            reasoning: { effort: reasoning_effort },
            ...params
        })
    }
});

/**
 * API Manager handles model interactions
 */
export class ApiManager {
    constructor(config = {}) {
        this.config = config;
        this.clients = new Map();
        this.initializeClients();
    }
    
    /**
     * Initialize API clients from environment
     */
    initializeClients() {
        // Initialize Kimi client if API key exists
        if (process.env.KIMI_API_KEY) {
            const kimiConfig = MODEL_REGISTRY.kimi;
            this.clients.set('kimi', {
                config: kimiConfig,
                client: kimiConfig.createClient(process.env.KIMI_API_KEY)
            });
            log('Initialized Kimi API client');
        }
        
        // Initialize O3 client if API key exists
        if (process.env.OPENAI_API_KEY) {
            const o3Config = MODEL_REGISTRY.o3;
            this.clients.set('o3', {
                config: o3Config,
                client: o3Config.createClient(process.env.OPENAI_API_KEY)
            });
            log('Initialized O3 API client');
        }
        
        if (this.clients.size === 0) {
            log('Warning: No API keys configured. Set KIMI_API_KEY or OPENAI_API_KEY');
        }
    }
    
    /**
     * Send a message to the specified model
     * 
     * @param {string} content - Message content
     * @param {string} model - Model name (kimi, o3)
     * @param {string} reasoning_effort - Reasoning effort level
     * @param {number} timeout - Timeout in ms
     * @param {AbortSignal|null} [abortSignal] - Abort signal for cancellation
     * @param {ChatMessage[]} [history] - Conversation history
     * @param {Tool[]|null} [tools] - Available tools for function calling
     * @returns {Promise<ApiResponse>} - Response object
     */
    async sendMessage(content, model = 'kimi', reasoning_effort = 'medium', timeout = 300000, abortSignal = null, history = [], tools = null) {
        const modelClient = this.clients.get(model);
        
        if (!modelClient) {
            throw new Error(`Model not available: ${model}. Check API key configuration.`);
        }
        
        const { config, client } = modelClient;
        
        // Build message history with system prompt
        const messages = [
            {
                role: 'system',
                content: 'You are a helpful AI assistant. You have access to the conversation history below.'
            }
        ];
        
        // Add history, but prevent consecutive messages from same role
        let lastRole = 'system';
        for (const msg of history) {
            if (msg.role === lastRole && msg.role === 'user') {
                // Merge consecutive user messages
                messages[messages.length - 1].content += '\n\n' + msg.content;
            } else {
                messages.push(msg);
                lastRole = msg.role;
            }
        }
        
        // Add the new user message
        if (lastRole === 'user') {
            // Merge with previous user message
            messages[messages.length - 1].content += '\n\n' + content;
        } else {
            messages.push({
                role: 'user',
                content
            });
        }
        
        // Format request according to model
        const requestData = config.formatRequest(
            messages,
            config.extraParams,
            reasoning_effort,
            tools && config.supportsTools ? tools : null
        );
        
        log(`Sending request to ${config.id}: ${content.slice(0, 50)}...`);
        
        // Debug log the full request in agent mode
        if (process.env.DEBUG_AGENTS === 'true') {
            log(`Full request to ${config.id}:`, JSON.stringify(requestData, null, 2));
        }
        
        try {
            // Create timeout
            // Note: We can't abort a signal that was passed to us
            // The caller should use AbortController if they want timeout control
            const timeoutId = setTimeout(() => {
                // Just for logging timeout, actual abort is handled by axios
            }, timeout);
            
            // Make API request
            const response = await client.post(
                config.endpoint,
                requestData,
                {
                    signal: abortSignal,
                    timeout
                }
            );
            
            clearTimeout(timeoutId);
            
            // Extract response based on model format
            let responseContent;
            let toolCalls = null;
            let finishReason = null;
            
            if (model === 'o3') {
                // O3 format
                const messageItem = response.data.output?.find(item => item.type === 'message');
                responseContent = messageItem?.content?.[0]?.text || response.data.completion || '';
                finishReason = 'stop'; // O3 doesn't have finish_reason
            } else {
                // OpenAI/Kimi format
                const choice = response.data.choices?.[0];
                responseContent = choice?.message?.content || '';
                toolCalls = choice?.message?.tool_calls || null;
                finishReason = choice?.finish_reason || null;
            }
            
            log(`Received response from ${config.id}: ${responseContent?.slice(0, 50) || '(tool calls)'}, finish_reason: ${finishReason}`);
            
            return {
                content: responseContent,
                model: config.id,
                usage: response.data.usage || {},
                toolCalls,
                finishReason
            };
            
        } catch (err) {
            if (err.name === 'AbortError' || err.code === 'ECONNABORTED') {
                const timeoutError = /** @type {any} */ (new Error(`Request timeout after ${timeout}ms`));
                timeoutError.timeout = true;
                throw timeoutError;
            }
            
            log(`API error from ${config.id}: ${err.message}`);
            throw err;
        }
    }
    
    /**
     * Get available models
     * 
     * @returns {string[]} - List of available model names
     */
    getAvailableModels() {
        return Array.from(this.clients.keys());
    }
    
    /**
     * Check if a model is available
     * 
     * @param {string} model - Model name
     * @returns {boolean} - True if available
     */
    isModelAvailable(model) {
        return this.clients.has(model);
    }
    
    /**
     * Get model information
     * 
     * @param {string} model - Model name
     * @returns {Object|null} - Model config or null
     */
    getModelInfo(model) {
        const client = this.clients.get(model);
        return client ? client.config : null;
    }
}

// Create singleton instance
export const apiManager = new ApiManager();