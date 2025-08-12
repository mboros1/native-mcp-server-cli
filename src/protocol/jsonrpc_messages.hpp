#pragma once

#include <string>
#include <optional>
#include <vector>
#include <json_struct.h>
#include <chrono>
#include <variant>
#include <random>
#include <limits>

namespace jsonrpc {

// ============================================================================
// JSON-RPC 2.0 Core Types
// ============================================================================

// Request ID Type - using int64_t for better range and thread safety
using RequestId = int64_t;

// ============================================================================
// Random ID Generation
// ============================================================================

/**
 * @brief Generate a random 32-bit request ID
 * 
 * Uses thread-local random generator to avoid contention.
 * We use 32-bit range to ensure compatibility with JSON parsers
 * that may have issues with large 64-bit integers.
 * IDs are in range [1, INT32_MAX] to avoid 0 and negative values.
 */
inline int64_t GenerateRequestId() {
    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_int_distribution<int32_t> dist(
        1, std::numeric_limits<int32_t>::max()
    );
    return static_cast<int64_t>(dist(gen));
}

// Standard error codes (from spec)
enum class ErrorCode : int {
    // JSON-RPC 2.0 standard errors
    ParseError     = -32700,  // Invalid JSON was received
    InvalidRequest = -32600,  // JSON sent is not a valid Request
    MethodNotFound = -32601,  // Method does not exist
    InvalidParams  = -32602,  // Invalid method parameters
    InternalError  = -32603,  // Internal JSON-RPC error
    
    // Implementation-defined server errors (-32000 to -32099)
    ServerError       = -32000,  // General server error
    Timeout          = -32001,  // Request timeout
    ModelUnavailable = -32002,  // AI model unavailable
    ToolFailed       = -32003,  // Tool execution failed
    RateLimitExceeded = -32004,  // Rate limit exceeded
    AuthFailed       = -32005,  // Authentication failed
    SyncError        = -32010,  // Chat history sync error
    FileError        = -32011,  // File operation error
};

// Convert error code to int for serialization
inline int errorCodeToInt(ErrorCode code) {
    return static_cast<int>(code);
}

// Helper for compact JSON serialization
template<typename T>
inline std::string toCompactJson(const T& obj) {
    return JS::serializeStruct(obj, JS::SerializerOptions(JS::SerializerOptions::Compact));
}

// ============================================================================
// Simple Serializable Structs (for json_struct)
// ============================================================================

// Forward declaration - will be defined after param structs
struct JsonRpcRequest;

// Generic JSON-RPC Response (for parsing, without specific types)
struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    int64_t id = 0;
    // Note: result and error deliberately omitted for parsing envelope only
    
    JS_OBJ(jsonrpc, id);
};

// Generic JSON-RPC Notification (no id, no response expected)
struct JsonRpcNotification {
    std::string jsonrpc = "2.0";
    std::string method;
    std::optional<std::string> params;  // JSON string of params
    
    JS_OBJ(jsonrpc, method, params);
};

// ============================================================================
// Parameter Structs (for building params)
// ============================================================================

struct ChatParams {
    std::string content;
    std::optional<std::string> model;
    std::optional<int> timeout;
    std::optional<std::string> reasoning_effort;
    
    JS_OBJ(content, model, timeout, reasoning_effort);
};

struct ToolExecuteParams {
    std::string name;
    std::optional<std::string> arguments;
    
    JS_OBJ(name, arguments);
};

struct HelloParams {
    std::string version = "2.0";
    std::string clientVersion;
    std::vector<std::string> capabilities;
    
    JS_OBJ(version, clientVersion, capabilities);
};

struct TransactionBeginParams {
    std::vector<std::string> operations;
    std::optional<int> timeout;
    
    JS_OBJ(operations, timeout);
};

// Cancel/interrupt request params
struct CancelParams {
    std::optional<int64_t> request_id;  // ID of request to cancel, null = cancel current
    
    JS_OBJ(request_id);
};

// Retry request params
struct RetryParams {
    std::string originalMessage;
    std::string model;
    std::optional<std::string> reasoning_effort;
    
    JS_OBJ(originalMessage, model, reasoning_effort);
};

// Sync request params
struct SyncParams {
    std::optional<int64_t> since_timestamp;  // Get messages since this timestamp
    std::optional<int> limit;  // Maximum number of messages
    
    JS_OBJ(since_timestamp, limit);
};

// Reload request params
struct ReloadParams {
    std::optional<std::string> source;  // "file", "memory", etc.
    
    JS_OBJ(source);
};

// Generic JSON-RPC Request - for parsing/serialization
struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::optional<int64_t> id;  // No id = notification
    std::string method;
    // Note: params deliberately omitted from this struct
    // We'll handle it separately based on method
    
    JS_OBJ(jsonrpc, id, method);
};

// Template for serializing requests with specific param types
template<typename ParamsType>
struct JsonRpcRequestWithParams {
    std::string jsonrpc = "2.0";
    std::optional<int64_t> id;
    std::string method;
    std::optional<ParamsType> params;
    
    JS_OBJ(jsonrpc, id, method, params);
};

// ============================================================================
// Result Structs (for parsing results)
// ============================================================================

struct ChatResult {
    std::string reply;
    int64_t timestamp;
    std::optional<bool> streaming;
    std::optional<std::string> streamId;
    
    JS_OBJ(reply, timestamp, streaming, streamId);
};

struct HelloResult {
    std::string version;
    std::string serverVersion;
    std::vector<std::string> capabilities;
    std::string sessionId;
    
    JS_OBJ(version, serverVersion, capabilities, sessionId);
};

struct ToolListResult {
    std::vector<std::string> tools;
    int64_t timestamp;
    
    JS_OBJ(tools, timestamp);
};

struct SyncResult {
    int entries;
    std::vector<std::string> messages;
    int64_t timestamp;
    
    JS_OBJ(entries, messages, timestamp);
};

struct ErrorInfo {
    int code;
    std::string message;
    std::optional<std::string> data;
    
    JS_OBJ(code, message, data);
};

// Template for serializing responses with specific result types
template<typename ResultType>
struct JsonRpcResponseWithResult {
    std::string jsonrpc = "2.0";
    int64_t id = 0;
    std::optional<ResultType> result;
    
    JS_OBJ(jsonrpc, id, result);
};

// Template for serializing error responses
struct JsonRpcErrorResponse {
    std::string jsonrpc = "2.0";
    int64_t id = 0;
    ErrorInfo error;
    
    JS_OBJ(jsonrpc, id, error);
};

// Cancel result
struct CancelResult {
    bool success;
    bool was_running;
    
    JS_OBJ(success, was_running);
};

// Simple response for operations that just return success/fail
struct SimpleResponse {
    bool success;
    std::optional<std::string> message;
    
    JS_OBJ(success, message);
};

// Rate limit information
struct RateLimitInfo {
    int limit = 0;
    int remaining = 0;
    int64_t reset_at = 0;  // Unix timestamp
    
    JS_OBJ(limit, remaining, reset_at);
};

// ============================================================================
// Notification Parameter Types (server -> client events)
// ============================================================================

// tool.called notification
struct ToolCalledParams {
    std::string tool_name;
    std::optional<std::string> arguments;  // JSON string
    int64_t timestamp;
    std::optional<int64_t> request_id;  // ID of the original request that triggered this tool call
    
    JS_OBJ(tool_name, arguments, timestamp, request_id);
};

// tool.result notification
struct ToolResultParams {
    std::string tool_name;
    std::string preview;
    int total_items;
    int64_t timestamp;
    std::optional<int64_t> request_id;  // ID of the original request that triggered this tool
    
    JS_OBJ(tool_name, preview, total_items, timestamp, request_id);
};

// Tool execution result
struct ToolExecuteResult {
    bool success;
    std::optional<std::string> output;
    std::optional<std::string> error;
    std::optional<int> execution_time_ms;
    
    JS_OBJ(success, output, error, execution_time_ms);
};

// stream.chunk notification
struct StreamChunkParams {
    std::string streamId;
    std::string chunk;
    int index;
    
    JS_OBJ(streamId, chunk, index);
};

// stream.end notification
struct StreamEndParams {
    std::string streamId;
    int totalChunks;
    
    JS_OBJ(streamId, totalChunks);
};

// ============================================================================
// Request Builder Class
// ============================================================================

class JsonRpcRequestBuilder {
private:
    std::string method_;
    std::optional<int64_t> id_;
    std::optional<std::string> params_json_;
    
public:
    JsonRpcRequestBuilder() = default;
    
    // Set the method
    JsonRpcRequestBuilder& method(const std::string& m) {
        method_ = m;
        return *this;
    }
    
    // Set custom ID (for testing or special cases)
    JsonRpcRequestBuilder& id(int64_t i) {
        id_ = i;
        return *this;
    }
    
    // Auto-generate random ID
    JsonRpcRequestBuilder& withId() {
        id_ = GenerateRequestId();
        return *this;
    }
    
    // No ID (notification)
    JsonRpcRequestBuilder& asNotification() {
        id_ = std::nullopt;
        return *this;
    }
    
    // Set params from a struct - serialize it properly
    template<typename T>
    JsonRpcRequestBuilder& params(const T& p) {
        // Special handling for std::monostate (no params)
        if constexpr (std::is_same_v<T, std::monostate>) {
            params_json_ = std::nullopt;
            return *this;
        } else {
            // Create a temporary request with params to serialize properly
            JsonRpcRequestWithParams<T> req;
            req.jsonrpc = "2.0";
            req.id = id_;
            req.method = method_;
            req.params = p;
            
            // Extract just the params part from the serialized JSON
            std::string full_json = toCompactJson(req);
            params_json_ = full_json;  // Store for later extraction
            return *this;
        }
    }
    
    // No params
    JsonRpcRequestBuilder& noParams() {
        params_json_ = std::nullopt;
        return *this;
    }
    
    // Build the final request (returns basic structure)
    JsonRpcRequest build() const {
        JsonRpcRequest req;
        req.jsonrpc = "2.0";
        req.id = id_;
        req.method = method_;
        return req;
    }
    
    // Build and serialize to JSON string with proper params handling
    template<typename T = std::monostate>
    std::string buildJson(const T& p = std::monostate{}) const {
        if constexpr (std::is_same_v<T, std::monostate>) {
            // No params case - use the stored params_json_ if available
            if (params_json_.has_value()) {
                // We already have the full JSON with params
                return params_json_.value();
            } else {
                // No params at all
                JsonRpcRequest req;
                req.jsonrpc = "2.0";
                req.id = id_;
                req.method = method_;
                return toCompactJson(req);
            }
        } else {
            // Params provided directly
            JsonRpcRequestWithParams<T> req;
            req.jsonrpc = "2.0";
            req.id = id_;
            req.method = method_;
            req.params = p;
            return toCompactJson(req);
        }
    }
    
    // Simplified buildJson that works with previously set params
    std::string buildJson() const {
        return buildJson<std::monostate>();
    }
    
    // Build JSON with raw params string (for dynamic cases)
    std::string buildJsonWithRawParams(const std::string& raw_params) const {
        std::string json = "{\"jsonrpc\":\"2.0\"";
        if (id_.has_value()) {
            json += ",\"id\":" + std::to_string(id_.value());
        }
        json += ",\"method\":\"" + method_ + "\"";
        if (!raw_params.empty() && raw_params != "{}") {
            json += ",\"params\":" + raw_params;
        }
        json += "}";
        return json;
    }
    
    // Static helper methods for common requests
    static JsonRpcRequest makeChat(const std::string& content, 
                                   const std::string& model = "kimi",
                                   int timeout = 120000) {
        ChatParams p;
        p.content = content;
        p.model = model;
        p.timeout = timeout;
        
        return JsonRpcRequestBuilder()
            .method("chat.send")
            .withId()
            .params(p)
            .build();
    }
    
    static JsonRpcRequest makeHello(const std::string& clientVersion,
                                    const std::vector<std::string>& capabilities) {
        HelloParams p;
        p.clientVersion = clientVersion;
        p.capabilities = capabilities;
        
        return JsonRpcRequestBuilder()
            .method("rpc.hello")
            .id(0)  // Special ID for hello
            .params(p)
            .build();
    }
    
    static JsonRpcRequest makeToolList() {
        return JsonRpcRequestBuilder()
            .method("tools.list")
            .withId()
            .noParams()
            .build();
    }
    
    static JsonRpcRequest makeSync() {
        return JsonRpcRequestBuilder()
            .method("history.sync")
            .withId()
            .noParams()
            .build();
    }
    
    static JsonRpcRequest makeCancel(std::optional<int64_t> targetId = std::nullopt) {
        CancelParams p;
        p.request_id = targetId;
        
        return JsonRpcRequestBuilder()
            .method("request.cancel")
            .withId()
            .params(p)
            .build();
    }
};

// ============================================================================
// Response Builder Class
// ============================================================================

class JsonRpcResponseBuilder {
private:
    int64_t id_ = 0;
    std::optional<std::string> result_json_;
    std::optional<ErrorInfo> error_;
    
public:
    JsonRpcResponseBuilder() = default;
    
    // Set the ID (must match request)
    JsonRpcResponseBuilder& id(int64_t i) {
        id_ = i;
        return *this;
    }
    
    // Set success result from struct
    template<typename T>
    JsonRpcResponseBuilder& result(const T& r) {
        // Store the result for later serialization
        JsonRpcResponseWithResult<T> resp;
        resp.jsonrpc = "2.0";
        resp.id = id_;
        resp.result = r;
        result_json_ = toCompactJson(resp);
        error_ = std::nullopt;  // Clear error if setting result
        return *this;
    }
    
    // Set error
    JsonRpcResponseBuilder& error(int code, const std::string& message, 
                                  const std::string& data = "") {
        ErrorInfo e;
        e.code = code;
        e.message = message;
        if (!data.empty()) {
            e.data = data;
        }
        
        error_ = e;
        result_json_ = std::nullopt;  // Clear result if setting error
        return *this;
    }
    
    // Build and serialize to JSON string
    std::string buildJson() const {
        if (error_.has_value()) {
            JsonRpcErrorResponse resp;
            resp.jsonrpc = "2.0";
            resp.id = id_;
            resp.error = error_.value();
            return toCompactJson(resp);
        } else if (result_json_.has_value()) {
            // Already serialized with proper type
            return result_json_.value();
        } else {
            // No result or error - shouldn't happen but handle gracefully
            JsonRpcResponse resp;
            resp.jsonrpc = "2.0";
            resp.id = id_;
            return toCompactJson(resp);
        }
    }
};

// ============================================================================
// Notification Builder Class
// ============================================================================

class JsonRpcNotificationBuilder {
private:
    JsonRpcNotification notification_;
    
public:
    JsonRpcNotificationBuilder() {
        notification_.jsonrpc = "2.0";
    }
    
    // Set the method
    JsonRpcNotificationBuilder& method(const std::string& m) {
        notification_.method = m;
        return *this;
    }
    
    // Set params from struct
    template<typename T>
    JsonRpcNotificationBuilder& params(const T& p) {
        notification_.params = toCompactJson(p);
        return *this;
    }
    
    // Set raw JSON params
    JsonRpcNotificationBuilder& paramsJson(const std::string& json) {
        notification_.params = json;
        return *this;
    }
    
    // Build the final notification
    JsonRpcNotification build() const {
        return notification_;
    }
    
    // Build and serialize to JSON string
    std::string buildJson() const {
        return toCompactJson(notification_);
    }
};

// ============================================================================
// Parser Helpers
// ============================================================================

class JsonRpcParser {
public:
    // Parse a request
    static bool parseRequest(const std::string& json, JsonRpcRequest& request) {
        try {
            JS::ParseContext context(json);
            auto error = context.parseTo(request);
            return error == JS::Error::NoError;
        } catch (...) {
            return false;
        }
    }
    
    // Parse a response
    static bool parseResponse(const std::string& json, JsonRpcResponse& response) {
        try {
            JS::ParseContext context(json);
            auto error = context.parseTo(response);
            return error == JS::Error::NoError;
        } catch (...) {
            return false;
        }
    }
    
    // Parse params into a specific type
    template<typename T>
    static bool parseParams(const std::string& params_json, T& params) {
        try {
            JS::ParseContext context(params_json);
            auto error = context.parseTo(params);
            return error == JS::Error::NoError;
        } catch (...) {
            return false;
        }
    }
    
    // Note: isError, getError, getResult methods removed as JsonRpcResponse
    // no longer contains result/error fields. Parsing should be done based on
    // the actual JSON structure using simdjson or after determining message type.
};

} // namespace jsonrpc