#pragma once

#include "jsonrpc_messages.hpp"
#include <functional>
#include <chrono>
#include <map>
#include <simdjson.h>

namespace jsonrpc {

// ============================================================================
// Procedure Definition Template
// ============================================================================

/**
 * @brief Type-safe procedure definition
 * @tparam ParamsType The type of parameters this procedure accepts
 * @tparam ResultType The type of result this procedure returns
 */
template<typename ParamsType, typename ResultType>
struct Procedure {
    const char* name;                          // JSON-RPC method name
    const char* description;                   // Human-readable description
    std::chrono::seconds default_timeout;      // Default timeout for this procedure
    
    // Could be extended with:
    // - Retry policy
    // - Priority level
    // - Rate limit configuration
    // - Required permissions
};

// ============================================================================
// PROCEDURE DEFINITIONS
// ============================================================================

/**
 * @brief Send a chat message to the AI assistant
 * 
 * This procedure sends a message to the AI model and receives a response.
 * The response may be streamed or returned as a complete message.
 * 
 * @param content The message text to send
 * @param model The model to use (e.g., "gpt-4", "claude-3")
 * @param timeout Optional timeout in seconds
 * @param reasoning_effort Optional reasoning effort level (0-100)
 * 
 * @returns ChatResult containing:
 *   - reply: The assistant's response text
 *   - timestamp: Server timestamp of response
 *   - streaming: Whether response is streaming
 *   - token_count: Optional token usage information
 * 
 * @errors
 *   - TIMEOUT (-32001): Request exceeded timeout
 *   - RATE_LIMIT (-32002): Rate limit exceeded
 *   - MODEL_ERROR (-32003): Model failed to respond
 * 
 * @example
 *   client.Call(CHAT_SEND, ChatParams{
 *       .content = "Hello",
 *       .model = "gpt-4"
 *   });
 */
inline constexpr Procedure<ChatParams, ChatResult> CHAT_SEND {
    .name = "chat.send",
    .description = "Send a message to the AI assistant",
    .default_timeout = std::chrono::seconds(120)  // Increased for agent mode and complex operations
};

/**
 * @brief List available tools/functions
 * 
 * Returns a list of all tools available in the current session.
 * Tools may be filtered based on capabilities or permissions.
 * 
 * @returns ToolListResult containing:
 *   - tools: Array of tool names
 *   - timestamp: Server timestamp
 * 
 * @errors
 *   - NOT_AUTHORIZED (-32010): Not authorized to list tools
 * 
 * @example
 *   client.Call(TOOLS_LIST, {});
 */
inline constexpr Procedure<std::monostate, ToolListResult> TOOLS_LIST {
    .name = "tools.list",
    .description = "List available tools",
    .default_timeout = std::chrono::seconds(5)
};

/**
 * @brief Execute a tool/function
 * 
 * Executes a specific tool with the provided arguments.
 * Tools are executed server-side and may have side effects.
 * 
 * @param tool_name Name of the tool to execute
 * @param arguments Tool-specific arguments (as JSON string or object)
 * 
 * @returns ToolExecuteResult containing:
 *   - success: Whether execution succeeded
 *   - output: Tool output (format depends on tool)
 *   - error: Error message if failed
 *   - execution_time_ms: Time taken in milliseconds
 * 
 * @errors
 *   - TOOL_NOT_FOUND (-32020): Tool doesn't exist
 *   - INVALID_ARGS (-32021): Invalid tool arguments
 *   - EXECUTION_ERROR (-32022): Tool execution failed
 *   - TIMEOUT (-32001): Tool execution timed out
 * 
 * @example
 *   client.Call(TOOL_EXECUTE, ToolExecuteParams{
 *       .tool_name = "search",
 *       .arguments = R"({"query": "weather"})"
 *   });
 */
inline constexpr Procedure<ToolExecuteParams, ToolExecuteResult> TOOL_EXECUTE {
    .name = "tool.execute",
    .description = "Execute a tool with given arguments",
    .default_timeout = std::chrono::seconds(60)
};

/**
 * @brief Synchronize conversation history
 * 
 * Retrieves conversation history from the server.
 * Used to restore context after reconnection or page refresh.
 * 
 * @param since_timestamp Get messages since this timestamp (0 for all)
 * @param limit Maximum number of messages to return
 * 
 * @returns SyncResult containing:
 *   - entries: Number of history entries
 *   - messages: Array of previous messages (JSON array)
 *   - timestamp: Current server timestamp
 * 
 * @example
 *   client.Call(HISTORY_SYNC, SyncParams{
 *       .since_timestamp = 0,
 *       .limit = 100
 *   });
 */
inline constexpr Procedure<SyncParams, SyncResult> HISTORY_SYNC {
    .name = "history.sync",
    .description = "Synchronize conversation history",
    .default_timeout = std::chrono::seconds(10)
};

/**
 * @brief Initial handshake with server
 * 
 * Establishes connection and exchanges capability information.
 * Should be the first call after connecting.
 * 
 * @returns HelloResult containing:
 *   - version: Server JSON-RPC version
 *   - serverVersion: Server implementation version
 *   - capabilities: Array of supported capabilities
 *   - sessionId: Unique session identifier
 * 
 * @example
 *   client.Call(HELLO, {});
 */
inline constexpr Procedure<std::monostate, HelloResult> HELLO {
    .name = "rpc.hello",
    .description = "Initial handshake with server",
    .default_timeout = std::chrono::seconds(5)
};

/**
 * @brief Cancel a running request
 * 
 * Attempts to cancel a previously sent request.
 * May not succeed if the request has already completed.
 * 
 * @param request_id ID of the request to cancel
 * 
 * @returns CancelResult containing:
 *   - success: Whether cancellation succeeded
 *   - was_running: Whether request was still running
 * 
 * @example
 *   client.Call(REQUEST_CANCEL, CancelParams{
 *       .request_id = pending_id
 *   });
 */
inline constexpr Procedure<CancelParams, CancelResult> REQUEST_CANCEL {
    .name = "request.cancel",
    .description = "Cancel a pending request",
    .default_timeout = std::chrono::seconds(2)
};

/**
 * @brief Retry the last chat message
 */
inline constexpr Procedure<std::monostate, ChatResult> CHAT_RETRY {
    .name = "chat.retry",
    .description = "Retry the last chat message",
    .default_timeout = std::chrono::seconds(120)
};

/**
 * @brief Clear conversation history
 */
inline constexpr Procedure<std::monostate, std::monostate> CHAT_CLEAR {
    .name = "chat.clear",
    .description = "Clear conversation history",
    .default_timeout = std::chrono::seconds(5)
};

/**
 * @brief Reload history from file
 */
inline constexpr Procedure<std::monostate, std::monostate> HISTORY_RELOAD {
    .name = "history.reload",
    .description = "Reload history from file",
    .default_timeout = std::chrono::seconds(5)
};

// ============================================================================
// Procedure Registry
// ============================================================================

/**
 * @brief Registry for managing procedure callbacks and request tracking
 * 
 * The registry maintains the mapping between request IDs and their callbacks,
 * handles response routing, and manages request lifecycle.
 */
class ProcedureRegistry {
public:
    using ErrorCallback = std::function<void(int code, const std::string& message)>;
    
private:
    struct PendingRequest {
        std::string method;
        std::function<void(const simdjson::dom::element&)> callback;
        ErrorCallback error_callback;
        std::chrono::steady_clock::time_point sent_time;
    };
    
    std::map<int64_t, PendingRequest> pending_requests_;
    ErrorCallback default_error_callback_;
    
public:
    /**
     * @brief Set a default error handler for all requests
     */
    void SetDefaultErrorHandler(ErrorCallback handler) {
        default_error_callback_ = handler;
    }
    
    /**
     * @brief Register a request with its callback
     * 
     * @tparam ResultType The expected result type
     * @param id The request ID
     * @param method The method name (for tracking)
     * @param callback Callback to invoke on success
     * @param error_callback Optional error callback (uses default if not provided)
     */
    template<typename ResultType>
    void RegisterRequest(
        int64_t id,
        const std::string& method,
        std::function<void(const ResultType&)> callback,
        ErrorCallback error_callback = nullptr
    ) {
        PendingRequest request{
            .method = method,
            .callback = [callback, method](const simdjson::dom::element& result_json) {
                // Parse the JSON into the result type
                try {
                    // For now, we'll parse manually - could use json_struct
                    ResultType result = ParseResult<ResultType>(result_json);
                    callback(result);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("Failed to parse result for method {}: {}", 
                                 method, e.what());
                }
            },
            .error_callback = error_callback ? error_callback : default_error_callback_,
            .sent_time = std::chrono::steady_clock::now()
        };
        
        pending_requests_[id] = std::move(request);
    }
    
    /**
     * @brief Handle a successful response
     * 
     * @param id The request ID from the response
     * @param result The result element from the JSON-RPC response
     * @return true if a handler was found and called
     */
    bool HandleResponse(int64_t id, const simdjson::dom::element& result) {
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end()) {
            it->second.callback(result);
            pending_requests_.erase(it);
            return true;
        }
        return false;
    }
    
    /**
     * @brief Handle an error response
     * 
     * @param id The request ID from the response
     * @param code The error code
     * @param message The error message
     * @return true if a handler was found and called
     */
    bool HandleError(int64_t id, int code, const std::string& message) {
        auto it = pending_requests_.find(id);
        if (it != pending_requests_.end()) {
            if (it->second.error_callback) {
                it->second.error_callback(code, message);
            }
            pending_requests_.erase(it);
            return true;
        }
        return false;
    }
    
    /**
     * @brief Clean up requests that have timed out
     * 
     * @param timeout Maximum age for pending requests
     * @return Number of requests cleaned up
     */
    size_t CleanupStaleRequests(std::chrono::seconds timeout) {
        auto now = std::chrono::steady_clock::now();
        size_t cleaned = 0;
        
        for (auto it = pending_requests_.begin(); it != pending_requests_.end();) {
            if (now - it->second.sent_time > timeout) {
                if (it->second.error_callback) {
                    it->second.error_callback(-32001, "Request timed out");
                }
                it = pending_requests_.erase(it);
                ++cleaned;
            } else {
                ++it;
            }
        }
        
        return cleaned;
    }
    
    /**
     * @brief Get the number of pending requests
     */
    size_t GetPendingCount() const {
        return pending_requests_.size();
    }
    
private:
    // Template specializations for parsing different result types
    template<typename T>
    static T ParseResult(const simdjson::dom::element& json);
};

// Template specializations for result parsing (implementations would go in .cpp)
template<>
inline ChatResult ProcedureRegistry::ParseResult<ChatResult>(const simdjson::dom::element& json) {
    ChatResult result;
    
    // Safe field extraction
    simdjson::dom::element elem;
    if (json.at_key("reply").get(elem) == simdjson::SUCCESS && elem.is_string()) {
        result.reply = std::string(elem.get_string().value());
    }
    if (json.at_key("timestamp").get(elem) == simdjson::SUCCESS && elem.is_int64()) {
        result.timestamp = elem.get_int64().value();
    }
    if (json.at_key("streaming").get(elem) == simdjson::SUCCESS && elem.is_bool()) {
        result.streaming = elem.get_bool().value();
    }
    
    return result;
}

template<>
inline ToolListResult ProcedureRegistry::ParseResult<ToolListResult>(const simdjson::dom::element& json) {
    ToolListResult result;
    
    simdjson::dom::element elem;
    if (json.at_key("tools").get(elem) == simdjson::SUCCESS && elem.is_array()) {
        for (auto tool : elem) {
            if (tool.is_string()) {
                result.tools.push_back(std::string(tool.get_string().value()));
            }
        }
    }
    if (json.at_key("timestamp").get(elem) == simdjson::SUCCESS && elem.is_int64()) {
        result.timestamp = elem.get_int64().value();
    }
    
    return result;
}

} // namespace jsonrpc