/**
 * JSON-RPC 2.0 Server Implementation
 * 
 * A modular, extensible JSON-RPC 2.0 server that matches the C++ client's expectations.
 * Designed for clean separation of concerns and easy testing.
 */

import { EventEmitter } from 'events';
import { log } from '../logger.js';

// JSON-RPC 2.0 Error Codes
export const JsonRpcErrorCodes = {
    PARSE_ERROR: -32700,
    INVALID_REQUEST: -32600,
    METHOD_NOT_FOUND: -32601,
    INVALID_PARAMS: -32602,
    INTERNAL_ERROR: -32603,
    // Custom error codes (-32000 to -32099)
    TIMEOUT: -32000,
    CANCELLED: -32001,
    MODEL_ERROR: -32002
};

/**
 * @typedef {Object} JsonRpcRequest
 * @property {string} jsonrpc - Must be "2.0"
 * @property {string} method - Method name to invoke
 * @property {any} [params] - Method parameters
 * @property {number|string} [id] - Request ID (omit for notifications)
 */

/**
 * @typedef {Object} JsonRpcResponse
 * @property {string} jsonrpc - Must be "2.0"
 * @property {any} [result] - Success result
 * @property {JsonRpcError} [error] - Error result
 * @property {number|string} id - Request ID
 */

/**
 * @typedef {Object} JsonRpcError
 * @property {number} code - Error code
 * @property {string} message - Error message
 * @property {any} [data] - Additional error data
 */

/**
 * JSON-RPC 2.0 Server
 * 
 * Handles JSON-RPC requests and responses with:
 * - Method registration and dispatch
 * - Request/response correlation
 * - Error handling
 * - Batch request support
 * - Notification support
 */
export class JsonRpcServer extends EventEmitter {
    constructor() {
        super();
        
        // Method handlers registry
        this.methods = new Map();
        
        // Active request tracking
        this.activeRequests = new Map();
        
        // Request statistics
        this.stats = {
            requestsReceived: 0,
            requestsSucceeded: 0,
            requestsFailed: 0,
            requestsCancelled: 0,
            notificationsReceived: 0
        };
    }
    
    /**
     * Register a method handler
     * 
     * @param {string} method - Method name
     * @param {Function} handler - Async handler function (params, context) => result
     * @param {Object} [options] - Handler options
     * @param {number} [options.timeout] - Request timeout in ms
     * @param {boolean} [options.allowNotification] - Allow as notification
     */
    registerMethod(method, handler, options = {}) {
        if (typeof handler !== 'function') {
            throw new TypeError('Handler must be a function');
        }
        
        this.methods.set(method, {
            handler,
            timeout: options.timeout || 300000, // 5 minutes default
            allowNotification: options.allowNotification || false
        });
        
        log(`Registered JSON-RPC method: ${method}`);
    }
    
    /**
     * Unregister a method
     * 
     * @param {string} method - Method name
     */
    unregisterMethod(method) {
        this.methods.delete(method);
        log(`Unregistered JSON-RPC method: ${method}`);
    }
    
    /**
     * Process a JSON-RPC request
     * 
     * @param {string} message - Raw JSON message
     * @param {Object} context - Request context (socket, clientId, etc)
     * @returns {Promise<string|null>} - JSON response or null for notifications
     */
    async processMessage(message, context = {}) {
        let request;
        
        // Parse JSON
        try {
            request = JSON.parse(message);
        } catch (err) {
            this.stats.requestsFailed++;
            return this.createErrorResponse(null, JsonRpcErrorCodes.PARSE_ERROR, 'Parse error');
        }
        
        // Handle batch requests
        if (Array.isArray(request)) {
            return this.processBatch(request, context);
        }
        
        // Process single request
        return this.processRequest(request, context);
    }
    
    /**
     * Process a single JSON-RPC request
     * 
     * @private
     */
    async processRequest(request, context) {
        // Validate JSON-RPC 2.0 format
        if (request.jsonrpc !== '2.0') {
            this.stats.requestsFailed++;
            return this.createErrorResponse(
                request.id || null,
                JsonRpcErrorCodes.INVALID_REQUEST,
                'Invalid Request - must specify jsonrpc: "2.0"'
            );
        }
        
        // Check if method exists
        const methodName = request.method;
        if (!methodName || typeof methodName !== 'string') {
            this.stats.requestsFailed++;
            return this.createErrorResponse(
                request.id || null,
                JsonRpcErrorCodes.INVALID_REQUEST,
                'Invalid Request - missing method'
            );
        }
        
        // Is this a notification? (no id field)
        const isNotification = !('id' in request);
        if (isNotification) {
            this.stats.notificationsReceived++;
        } else {
            this.stats.requestsReceived++;
        }
        
        // Get method handler
        const methodInfo = this.methods.get(methodName);
        if (!methodInfo) {
            log(`Method not found: ${methodName}`);
            this.stats.requestsFailed++;
            
            if (isNotification) {
                return null; // No response for notifications
            }
            
            return this.createErrorResponse(
                request.id,
                JsonRpcErrorCodes.METHOD_NOT_FOUND,
                `Method not found: ${methodName}`
            );
        }
        
        // Check if notification is allowed
        if (isNotification && !methodInfo.allowNotification) {
            log(`Notification not allowed for method: ${methodName}`);
            return null; // Silent fail for notifications
        }
        
        // Track active request
        const requestId = request.id;
        if (requestId !== undefined) {
            this.activeRequests.set(requestId, {
                method: methodName,
                startTime: Date.now(),
                context
            });
        }
        
        try {
            // Set up timeout
            const timeoutMs = methodInfo.timeout;
            const timeoutPromise = new Promise((_, reject) => {
                setTimeout(() => reject(new Error('Request timeout')), timeoutMs);
            });
            
            // Execute handler with timeout
            const result = await Promise.race([
                methodInfo.handler(request.params, context),
                timeoutPromise
            ]);
            
            // Success
            this.stats.requestsSucceeded++;
            
            if (isNotification) {
                return null; // No response for notifications
            }
            
            return this.createSuccessResponse(requestId, result);
            
        } catch (err) {
            // Error handling
            this.stats.requestsFailed++;
            log(`Error processing ${methodName}: ${err.message}`);
            
            if (isNotification) {
                return null; // No response for notifications
            }
            
            // Determine error code
            let errorCode = JsonRpcErrorCodes.INTERNAL_ERROR;
            let errorMessage = err.message || 'Internal error';
            let errorData = undefined;
            
            if (err.message === 'Request timeout') {
                errorCode = JsonRpcErrorCodes.TIMEOUT;
                errorMessage = 'Request timeout';
            } else if (err.cancelled) {
                errorCode = JsonRpcErrorCodes.CANCELLED;
                errorMessage = 'Request cancelled';
            } else if (err.code) {
                errorCode = err.code;
                errorData = err.data;
            }
            
            return this.createErrorResponse(requestId, errorCode, errorMessage, errorData);
            
        } finally {
            // Clean up active request
            if (requestId !== undefined) {
                this.activeRequests.delete(requestId);
            }
        }
    }
    
    /**
     * Process a batch of requests
     * 
     * @private
     */
    async processBatch(requests, context) {
        if (requests.length === 0) {
            return this.createErrorResponse(
                null,
                JsonRpcErrorCodes.INVALID_REQUEST,
                'Invalid Request - empty batch'
            );
        }
        
        // Process all requests in parallel
        const responses = await Promise.all(
            requests.map(req => this.processRequest(req, context))
        );
        
        // Filter out null responses (from notifications)
        const validResponses = responses.filter(r => r !== null);
        
        // Return null if all were notifications
        if (validResponses.length === 0) {
            return null;
        }
        
        // Return batch response
        return JSON.stringify(validResponses);
    }
    
    /**
     * Cancel an active request
     * 
     * @param {number|string} requestId - Request ID to cancel
     * @returns {boolean} - True if cancelled
     */
    cancelRequest(requestId) {
        const request = this.activeRequests.get(requestId);
        if (request) {
            this.activeRequests.delete(requestId);
            this.stats.requestsCancelled++;
            
            // Emit cancellation event for handlers to listen to
            this.emit('request:cancelled', requestId, request);
            
            log(`Cancelled request ${requestId} (${request.method})`);
            return true;
        }
        return false;
    }
    
    /**
     * Get active request info
     * 
     * @param {number|string} requestId - Request ID
     * @returns {Object|null} - Request info or null
     */
    getActiveRequest(requestId) {
        return this.activeRequests.get(requestId) || null;
    }
    
    /**
     * Create a JSON-RPC success response
     * 
     * @private
     */
    createSuccessResponse(id, result) {
        return JSON.stringify({
            jsonrpc: '2.0',
            result,
            id
        });
    }
    
    /**
     * Create a JSON-RPC error response
     * 
     * @private
     */
    createErrorResponse(id, code, message, data) {
        const error = {
            code,
            message
        };
        
        if (data !== undefined) {
            error.data = data;
        }
        
        return JSON.stringify({
            jsonrpc: '2.0',
            error,
            id
        });
    }
    
    /**
     * Get server statistics
     * 
     * @returns {Object} - Server stats
     */
    getStats() {
        return {
            ...this.stats,
            activeRequests: this.activeRequests.size,
            registeredMethods: this.methods.size
        };
    }
    
    /**
     * Reset statistics
     */
    resetStats() {
        this.stats = {
            requestsReceived: 0,
            requestsSucceeded: 0,
            requestsFailed: 0,
            requestsCancelled: 0,
            notificationsReceived: 0
        };
    }
}

// Singleton instance for the main server
export const jsonRpcServer = new JsonRpcServer();