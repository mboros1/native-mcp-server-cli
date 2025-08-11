import axios from 'axios';

/**
 * API Client Manager
 * Manages different AI model API clients and their configurations
 */
class APIClientManager {
    constructor() {
        this.models = new Map();
    }

    /**
     * Register a model configuration
     * @param {string} key - Model identifier
     * @param {Object} config - Model configuration
     */
    registerModel(key, config) {
        this.models.set(key, {
            id: config.id,
            client: config.client,
            endpoint: config.endpoint,
            extraParams: config.extraParams || {},
            supportsTools: config.supportsTools || false,
            formatRequest: config.formatRequest,
            parseResponse: config.parseResponse || ((resp) => resp)
        });
    }

    /**
     * Get model configuration
     * @param {string} key - Model identifier
     * @returns {Object|null} - Model config or null
     */
    getModel(key) {
        return this.models.get(key) || null;
    }

    /**
     * Check if model exists
     * @param {string} key - Model identifier
     * @returns {boolean}
     */
    hasModel(key) {
        return this.models.has(key);
    }

    /**
     * Create API request for a model
     * @param {string} modelKey - Model identifier
     * @param {Array} messages - Chat messages
     * @param {Object} params - Additional parameters
     * @param {Array|null} tools - Available tools
     * @returns {Object} - Formatted request
     */
    createRequest(modelKey, messages, params = {}, tools = null) {
        const model = this.getModel(modelKey);
        if (!model) {
            throw new Error(`Unknown model: ${modelKey}`);
        }

        const mergedParams = { ...model.extraParams, ...params };
        
        if (model.formatRequest) {
            return model.formatRequest(messages, mergedParams, params.reasoning_effort, tools);
        }

        // Default format
        return {
            model: model.id,
            messages,
            ...(tools && model.supportsTools && tools.length > 0 ? { tools } : {}),
            ...mergedParams
        };
    }

    /**
     * Make API call for a model
     * @param {string} modelKey - Model identifier
     * @param {Object} requestData - Request data
     * @returns {Promise<Object>} - API response
     */
    async callAPI(modelKey, requestData) {
        const model = this.getModel(modelKey);
        if (!model) {
            throw new Error(`Unknown model: ${modelKey}`);
        }

        const response = await model.client.post(model.endpoint, requestData);
        
        if (model.parseResponse) {
            return model.parseResponse(response.data);
        }
        
        return response.data;
    }

    /**
     * Create default client with common settings
     * @param {string} baseURL - API base URL
     * @param {string} apiKey - API key
     * @param {number} timeout - Timeout in ms
     * @returns {Object} - Axios client instance
     */
    static createClient(baseURL, apiKey, timeout = 300000) {
        return axios.create({
            baseURL,
            timeout,
            headers: {
                'Content-Type': 'application/json',
                Authorization: `Bearer ${apiKey}`
            }
        });
    }
}

export default APIClientManager;