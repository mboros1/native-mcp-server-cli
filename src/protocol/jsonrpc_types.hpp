#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <chrono>
#include <json_struct/json_struct.h>

namespace jsonrpc {

// ============================================================================
// JSON-RPC 2.0 Core Types
// ============================================================================

// JSON-RPC version constant
constexpr const char* JSONRPC_VERSION = "2.0";

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

// Convert error code to string for serialization
inline int errorCodeToInt(ErrorCode code) {
    return static_cast<int>(code);
}

// ============================================================================
// Request ID Type (can be string, number, or null)
// ============================================================================
using RequestId = std::variant<int, std::string, std::monostate>;

// Custom serialization for RequestId
} // Close jsonrpc namespace temporarily

namespace JS {
template<>
struct TypeHandler<jsonrpc::RequestId> {
    static inline Error to(RequestId& to_type, ParseContext& context) {
        // Try integer first
        int int_val;
        auto int_error = TypeHandler<int>::to(int_val, context);
        if (int_error == Error::NoError) {
            to_type = int_val;
            return Error::NoError;
        }
        
        // Try string
        std::string str_val;
        auto str_error = TypeHandler<std::string>::to(str_val, context);
        if (str_error == Error::NoError) {
            to_type = str_val;
            return Error::NoError;
        }
        
        // Check for null
        if (context.token.value == "null" || context.token.value.empty()) {
            to_type = std::monostate{};
            return Error::NoError;
        }
        
        return Error::UnknownError;
    }
    
    static inline void from(const RequestId& from_type, Token& token, Serializer& serializer) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int>) {
                TypeHandler<int>::from(arg, token, serializer);
            } else if constexpr (std::is_same_v<T, std::string>) {
                TypeHandler<std::string>::from(arg, token, serializer);
            } else {
                // monostate = null
                token.value = "null";
            }
        }, from_type);
    }
};
} // namespace JS

namespace jsonrpc { // Reopen jsonrpc namespace

// ============================================================================
// Error Object
// ============================================================================
struct ErrorObject {
    int code;
    std::string message;
    std::optional<std::string> data;  // Optional additional error info
    
    JS_OBJ(code, message, data);
};

// ============================================================================
// Base Request (all requests have these fields)
// ============================================================================
struct Request {
    std::string jsonrpc = JSONRPC_VERSION;
    std::optional<RequestId> id;  // No id = notification
    std::string method;
    
    // Check if this is a notification (no response expected)
    bool isNotification() const {
        return !id.has_value();
    }
    
    JS_OBJ(jsonrpc, id, method);
};

// ============================================================================
// Base Response
// ============================================================================
struct Response {
    std::string jsonrpc = JSONRPC_VERSION;
    RequestId id;  // Must match request ID
    
    JS_OBJ(jsonrpc, id);
};

// ============================================================================
// Method-Specific Parameter Types
// ============================================================================

// chat.send parameters
struct ChatParams {
    std::string content;
    std::optional<int> timeout;  // milliseconds
    std::optional<std::string> model;  // "kimi" or "o3"
    std::optional<std::string> reasoning_effort;  // for o3
    std::optional<std::string> transactionId;  // for multi-step operations
    
    JS_OBJ(content, timeout, model, reasoning_effort, transactionId);
};

// chat.retry parameters
struct RetryParams {
    std::optional<std::string> originalMessage;
    std::optional<std::string> model;
    std::optional<std::string> reasoning_effort;
    
    JS_OBJ(originalMessage, model, reasoning_effort);
};

// tools.execute parameters
struct ToolExecuteParams {
    std::string name;
    std::optional<std::string> arguments;  // JSON string of arguments
    
    JS_OBJ(name, arguments);
};

// request.cancel parameters
struct CancelParams {
    std::optional<RequestId> requestId;  // null = cancel current
    
    JS_OBJ(requestId);
};

// rpc.hello parameters (version negotiation)
struct HelloParams {
    std::string version;
    std::string clientVersion;
    std::vector<std::string> capabilities;
    
    JS_OBJ(version, clientVersion, capabilities);
};

// transaction.begin parameters
struct TransactionBeginParams {
    std::vector<std::string> operations;
    std::optional<int> timeout;
    
    JS_OBJ(operations, timeout);
};

// transaction.commit/rollback parameters
struct TransactionEndParams {
    std::string transactionId;
    
    JS_OBJ(transactionId);
};

// ============================================================================
// Method-Specific Result Types
// ============================================================================

// Rate limit information
struct RateLimitInfo {
    int limit;
    int remaining;
    int64_t reset;  // Unix timestamp
    
    JS_OBJ(limit, remaining, reset);
};

// Response metadata
struct ResponseMeta {
    std::optional<RateLimitInfo> rateLimit;
    std::optional<std::string> traceId;
    std::optional<std::string> spanId;
    
    JS_OBJ(rateLimit, traceId, spanId);
};

// chat.send result
struct ChatResult {
    std::string reply;
    int64_t timestamp;
    std::optional<bool> streaming;  // true if response will be streamed
    std::optional<std::string> streamId;  // ID for stream chunks
    
    JS_OBJ(reply, timestamp, streaming, streamId);
};

// rpc.hello result
struct HelloResult {
    std::string version;
    std::string serverVersion;
    std::vector<std::string> capabilities;
    std::string sessionId;
    std::optional<RateLimitInfo> rateLimit;
    
    JS_OBJ(version, serverVersion, capabilities, sessionId, rateLimit);
};

// history.sync result
struct SyncResult {
    int entries;
    std::vector<std::string> messages;  // Simplified for now
    int64_t timestamp;
    
    JS_OBJ(entries, messages, timestamp);
};

// tools.list result
struct ToolListResult {
    std::vector<std::string> tools;  // Tool names
    int64_t timestamp;
    
    JS_OBJ(tools, timestamp);
};

// tools.execute result
struct ToolExecuteResult {
    std::string tool_name;
    bool success;
    std::optional<std::string> result;  // JSON string
    std::optional<std::string> error;
    int64_t timestamp;
    
    JS_OBJ(tool_name, success, result, error, timestamp);
};

// transaction.begin result
struct TransactionBeginResult {
    std::string transactionId;
    int64_t expires;  // Unix timestamp when transaction expires
    
    JS_OBJ(transactionId, expires);
};

// ============================================================================
// Notification Parameter Types (no response expected)
// ============================================================================

// tool.called notification
struct ToolCalledParams {
    std::string tool_name;
    std::optional<std::string> arguments;  // JSON string
    int64_t timestamp;
    std::optional<int> request_id;  // ID of the original request that triggered this tool call
    
    JS_OBJ(tool_name, arguments, timestamp, request_id);
};

// tool.result notification
struct ToolResultParams {
    std::string tool_name;
    std::string preview;
    int total_items;
    int64_t timestamp;
    std::optional<int> request_id;  // ID of the original request that triggered this tool
    
    JS_OBJ(tool_name, preview, total_items, timestamp, request_id);
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

// sync.required notification
struct SyncRequiredParams {
    std::string reason;
    std::string action;
    std::optional<std::string> path;
    
    JS_OBJ(reason, action, path);
};

// ============================================================================
// Full Request Types (with params)
// ============================================================================

template<typename TParams>
struct RequestWithParams : Request {
    TParams params;
    
    // Note: Template structs need manual serialization helpers
    JS_OBJECT_WITH_SUPER(RequestWithParams, Request,
        params
    );
};

// Convenience type aliases
using ChatRequest = RequestWithParams<ChatParams>;
using RetryRequest = RequestWithParams<RetryParams>;
using ToolExecuteRequest = RequestWithParams<ToolExecuteParams>;
using CancelRequest = RequestWithParams<CancelParams>;
using HelloRequest = RequestWithParams<HelloParams>;
using TransactionBeginRequest = RequestWithParams<TransactionBeginParams>;
using TransactionEndRequest = RequestWithParams<TransactionEndParams>;

// Empty params for methods that don't need parameters
struct EmptyParams {
    // Empty struct, no members to serialize
    JS_OBJECT();
};

using SyncRequest = RequestWithParams<EmptyParams>;
using ReloadRequest = RequestWithParams<EmptyParams>;
using ToolListRequest = RequestWithParams<EmptyParams>;

// ============================================================================
// Full Response Types
// ============================================================================

// Success response with result
template<typename TResult>
struct SuccessResponse : Response {
    TResult result;
    std::optional<ResponseMeta> meta;
    
    // Note: Template structs need manual serialization helpers
    JS_OBJECT_WITH_SUPER(SuccessResponse, Response,
        result, meta
    );
};

// Error response
struct ErrorResponse : Response {
    ErrorObject error;
    std::optional<ResponseMeta> meta;
    
    JS_OBJECT_WITH_SUPER(ErrorResponse, Response,
        error, meta
    );
};

// Convenience type aliases for responses
using ChatResponse = SuccessResponse<ChatResult>;
using HelloResponse = SuccessResponse<HelloResult>;
using SyncResponse = SuccessResponse<SyncResult>;
using ToolListResponse = SuccessResponse<ToolListResult>;
using ToolExecuteResponse = SuccessResponse<ToolExecuteResult>;
using TransactionBeginResponse = SuccessResponse<TransactionBeginResult>;

// Simple success response (just acknowledges operation)
struct SimpleResult {
    bool success = true;
    std::optional<std::string> message;
    
    JS_OBJ(success, message);
};

using SimpleResponse = SuccessResponse<SimpleResult>;

// ============================================================================
// Notification Types (no id field)
// ============================================================================

template<typename TParams>
struct Notification {
    std::string jsonrpc = JSONRPC_VERSION;
    std::string method;
    TParams params;
    
    JS_OBJ(jsonrpc, method, params);
};

// Convenience type aliases for notifications
using ToolCalledNotification = Notification<ToolCalledParams>;
using ToolResultNotification = Notification<ToolResultParams>;
using StreamChunkNotification = Notification<StreamChunkParams>;
using StreamEndNotification = Notification<StreamEndParams>;
using SyncRequiredNotification = Notification<SyncRequiredParams>;

// ============================================================================
// Batch Request/Response
// ============================================================================

using BatchRequest = std::vector<Request>;
using BatchResponse = std::vector<Response>;

// ============================================================================
// Request Builder Helpers
// ============================================================================

class RequestBuilder {
private:
    static int next_id_;
    
public:
    // Generate next request ID
    static RequestId nextId() {
        return ++next_id_;
    }
    
    // Build a chat request
    static ChatRequest chat(const std::string& content, 
                           const std::string& model = "kimi",
                           int timeout = 120000) {
        ChatRequest req;
        req.id = nextId();
        req.method = "chat.send";
        req.params.content = content;
        req.params.model = model;
        req.params.timeout = timeout;
        return req;
    }
    
    // Build a sync request
    static SyncRequest sync() {
        SyncRequest req;
        req.id = nextId();
        req.method = "history.sync";
        return req;
    }
    
    // Build a reload request
    static ReloadRequest reload() {
        ReloadRequest req;
        req.id = nextId();
        req.method = "history.reload";
        return req;
    }
    
    // Build a tool list request
    static ToolListRequest toolList() {
        ToolListRequest req;
        req.id = nextId();
        req.method = "tools.list";
        return req;
    }
    
    // Build a cancel request
    static CancelRequest cancel(std::optional<RequestId> targetId = std::nullopt) {
        CancelRequest req;
        req.id = nextId();
        req.method = "request.cancel";
        req.params.requestId = targetId;
        return req;
    }
    
    // Build a hello request for version negotiation
    static HelloRequest hello(const std::string& clientVersion,
                             const std::vector<std::string>& capabilities) {
        HelloRequest req;
        req.id = 0;  // Special ID for hello
        req.method = "rpc.hello";
        req.params.version = JSONRPC_VERSION;
        req.params.clientVersion = clientVersion;
        req.params.capabilities = capabilities;
        return req;
    }
};

// Initialize static member
inline int RequestBuilder::next_id_ = 0;

// ============================================================================
// Response Parser Helpers
// ============================================================================

class ResponseParser {
public:
    // Parse a generic JSON-RPC response
    template<typename T>
    static bool parse(const std::string& json, T& result) {
        try {
            JS::ParseContext context(json);
            auto error = context.parseTo(result);
            return error == JS::Error::NoError;
        } catch (...) {
            return false;
        }
    }
    
    // Check if response is an error
    static bool isError(const std::string& json) {
        return json.find("\"error\"") != std::string::npos;
    }
    
    // Extract request ID from response
    static std::optional<RequestId> extractId(const std::string& json) {
        // Simple extraction - could be improved
        size_t id_pos = json.find("\"id\"");
        if (id_pos == std::string::npos) {
            return std::nullopt;
        }
        // This is simplified - real implementation would parse properly
        return std::nullopt;
    }
};

// ============================================================================
// Validation Helpers
// ============================================================================

class Validator {
public:
    // Validate that a request is well-formed
    template<typename T>
    static bool validateRequest(const T& request) {
        // Check JSON-RPC version
        if (request.jsonrpc != JSONRPC_VERSION) {
            return false;
        }
        
        // Check method is not empty
        if (request.method.empty()) {
            return false;
        }
        
        // Method names that begin with "rpc." are reserved
        if (request.method.find("rpc.") == 0 && 
            request.method != "rpc.hello") {
            return false;
        }
        
        return true;
    }
    
    // Validate response matches request
    static bool validateResponse(const Response& response, 
                                const Request& request) {
        // Version must match
        if (response.jsonrpc != JSONRPC_VERSION) {
            return false;
        }
        
        // If request was not a notification, IDs must match
        if (!request.isNotification()) {
            // Compare request and response IDs
            // This needs proper variant comparison
            return true;  // Simplified
        }
        
        return true;
    }
};

} // namespace jsonrpc