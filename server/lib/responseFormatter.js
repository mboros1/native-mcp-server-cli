/**
 * Response Formatter
 * Standardizes response formats for different message types
 */
class ResponseFormatter {
    /**
     * Format a chat response
     * @param {string} reply - Response content
     * @param {Object} original - Original request
     * @returns {string} - JSON string with newline
     */
    static chatResponse(reply, original = null) {
        return JSON.stringify({
            type: 'response',
            ...(original ? { original } : {}),
            reply,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a system message
     * @param {string} message - System message
     * @returns {string} - JSON string with newline
     */
    static systemMessage(message) {
        return JSON.stringify({
            type: 'system',
            message,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format an error response
     * @param {string} message - Error message
     * @param {boolean} canRetry - Whether retry is available
     * @param {string} originalMessage - Original message for retry
     * @returns {string} - JSON string with newline
     */
    static errorResponse(message, canRetry = false, originalMessage = null) {
        if (canRetry && originalMessage) {
            return JSON.stringify({
                type: 'timeout_error',
                message,
                canRetry: true,
                originalMessage,
                timestamp: Date.now()
            }) + '\n';
        }
        
        return JSON.stringify({
            type: 'error',
            message,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a tool call event
     * @param {string} toolName - Tool name
     * @param {Object} args - Tool arguments
     * @returns {string} - JSON string with newline
     */
    static toolCallEvent(toolName, args) {
        return JSON.stringify({
            type: 'tool_call',
            tool_name: toolName,
            arguments: args,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a tool result preview
     * @param {string} toolName - Tool name
     * @param {string} preview - Result preview
     * @param {number} totalItems - Total items count
     * @returns {string} - JSON string with newline
     */
    static toolResultPreview(toolName, preview, totalItems = 0) {
        return JSON.stringify({
            type: 'tool_result_preview',
            tool_name: toolName,
            preview,
            total_items: totalItems,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a tool error event
     * @param {string} toolName - Tool name
     * @param {string} error - Error message
     * @returns {string} - JSON string with newline
     */
    static toolErrorEvent(toolName, error) {
        return JSON.stringify({
            type: 'tool_error',
            tool_name: toolName,
            error,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a tool info message
     * @param {string} message - Info message
     * @returns {string} - JSON string with newline
     */
    static toolInfoMessage(message) {
        return JSON.stringify({
            type: 'tool_info',
            message,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a sync response
     * @param {string} serverStats - Server statistics
     * @param {Array} serverHistory - Server chat history
     * @returns {string} - JSON string with newline
     */
    static syncResponse(serverStats, serverHistory) {
        return JSON.stringify({
            type: 'sync_response',
            server_stats: serverStats,
            server_history: serverHistory,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a tool list response
     * @param {Array} tools - Available tools
     * @returns {string} - JSON string with newline
     */
    static toolListResponse(tools) {
        return JSON.stringify({
            type: 'tool_list_response',
            tools,
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a tool execution result
     * @param {string} toolName - Tool name
     * @param {boolean} success - Success status
     * @param {Object} result - Result or error
     * @returns {string} - JSON string with newline
     */
    static toolExecutionResult(toolName, success, result) {
        return JSON.stringify({
            type: 'tool_result',
            tool_name: toolName,
            success,
            ...(success ? { result } : { error: result }),
            timestamp: Date.now()
        }) + '\n';
    }

    /**
     * Format a heartbeat message
     * @returns {string} - JSON string with newline
     */
    static heartbeat() {
        return JSON.stringify({
            type: 'heartbeat',
            message: 'Server is alive',
            timestamp: Date.now()
        }) + '\n';
    }
}

export default ResponseFormatter;