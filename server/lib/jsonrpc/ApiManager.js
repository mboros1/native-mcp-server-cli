/**
 * API Manager for JSON-RPC Server
 * 
 * Manages API clients and model interactions
 */

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
     * @param {AbortSignal} [abortSignal] - Abort signal for cancellation
     * @param {Array} [history] - Conversation history
     * @param {Array} [tools] - Available tools for function calling
     * @returns {Promise<Object>} - Response object
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
            },
            ...history,
            {
                role: 'user',
                content
            }
        ];
        
        // Format request according to model
        const requestData = config.formatRequest(
            messages,
            config.extraParams,
            reasoning_effort,
            tools && config.supportsTools ? tools : null
        );
        
        log(`Sending request to ${config.id}: ${content.slice(0, 50)}...`);
        
        try {
            // Create timeout
            const timeoutId = setTimeout(() => {
                if (abortSignal) {
                    abortSignal.abort();
                }
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
            
            if (model === 'o3') {
                // O3 format
                const messageItem = response.data.output?.find(item => item.type === 'message');
                responseContent = messageItem?.content?.[0]?.text || response.data.completion || '';
            } else {
                // OpenAI/Kimi format
                const choice = response.data.choices?.[0];
                responseContent = choice?.message?.content || '';
                toolCalls = choice?.message?.tool_calls || null;
            }
            
            log(`Received response from ${config.id}: ${responseContent?.slice(0, 50) || '(tool calls)'}...`);
            
            return {
                content: responseContent,
                model: config.id,
                usage: response.data.usage || {},
                toolCalls
            };
            
        } catch (err) {
            if (err.name === 'AbortError' || err.code === 'ECONNABORTED') {
                const timeoutError = new Error(`Request timeout after ${timeout}ms`);
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