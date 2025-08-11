#pragma once

#include <string>
#include <optional>
#include <vector>
#include <json_struct.h>
#include <chrono>
#include <variant>

namespace jsonrpc {

// Helper for compact JSON serialization
template<typename T>
inline std::string toCompactJson(const T& obj) {
    return JS::serializeStruct(obj, JS::SerializerOptions(JS::SerializerOptions::Compact));
}

// ============================================================================
// Simple Serializable Structs (for json_struct)
// ============================================================================

// Generic JSON-RPC Request
struct JsonRpcRequest {
    std::string jsonrpc = "2.0";
    std::optional<int> id;  // No id = notification
    std::string method;
    std::optional<std::string> params;  // JSON string of params
    
    JS_OBJ(jsonrpc, id, method, params);
};

// Generic JSON-RPC Response
struct JsonRpcResponse {
    std::string jsonrpc = "2.0";
    int id = 0;
    std::optional<std::string> result;  // JSON string of result
    std::optional<std::string> error;   // JSON string of error
    
    JS_OBJ(jsonrpc, id, result, error);
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

// ============================================================================
// Request Builder Class
// ============================================================================

class JsonRpcRequestBuilder {
private:
    static int next_id_;
    JsonRpcRequest request_;
    
public:
    JsonRpcRequestBuilder() {
        request_.jsonrpc = "2.0";
    }
    
    // Set the method
    JsonRpcRequestBuilder& method(const std::string& m) {
        request_.method = m;
        return *this;
    }
    
    // Set custom ID
    JsonRpcRequestBuilder& id(int i) {
        request_.id = i;
        return *this;
    }
    
    // Auto-generate ID
    JsonRpcRequestBuilder& withId() {
        request_.id = ++next_id_;
        return *this;
    }
    
    // No ID (notification)
    JsonRpcRequestBuilder& asNotification() {
        request_.id = std::nullopt;
        return *this;
    }
    
    // Set params from a struct
    template<typename T>
    JsonRpcRequestBuilder& params(const T& p) {
        request_.params = toCompactJson(p);
        return *this;
    }
    
    // Set raw JSON params
    JsonRpcRequestBuilder& paramsJson(const std::string& json) {
        request_.params = json;
        return *this;
    }
    
    // Build the final request
    JsonRpcRequest build() const {
        return request_;
    }
    
    // Build and serialize to JSON string
    std::string buildJson() const {
        return toCompactJson(request_);
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
            .paramsJson("{}")
            .build();
    }
    
    static JsonRpcRequest makeSync() {
        return JsonRpcRequestBuilder()
            .method("history.sync")
            .withId()
            .paramsJson("{}")
            .build();
    }
    
    static JsonRpcRequest makeCancel(std::optional<int> targetId = std::nullopt) {
        std::string params_json = targetId.has_value() 
            ? "{\"requestId\":" + std::to_string(*targetId) + "}"
            : "{\"requestId\":null}";
            
        return JsonRpcRequestBuilder()
            .method("request.cancel")
            .withId()
            .paramsJson(params_json)
            .build();
    }
};

// Initialize static member
inline int JsonRpcRequestBuilder::next_id_ = 0;

// ============================================================================
// Response Builder Class
// ============================================================================

class JsonRpcResponseBuilder {
private:
    JsonRpcResponse response_;
    
public:
    JsonRpcResponseBuilder() {
        response_.jsonrpc = "2.0";
    }
    
    // Set the ID (must match request)
    JsonRpcResponseBuilder& id(int i) {
        response_.id = i;
        return *this;
    }
    
    // Set success result from struct
    template<typename T>
    JsonRpcResponseBuilder& result(const T& r) {
        response_.result = toCompactJson(r);
        response_.error = std::nullopt;  // Clear error if setting result
        return *this;
    }
    
    // Set raw JSON result
    JsonRpcResponseBuilder& resultJson(const std::string& json) {
        response_.result = json;
        response_.error = std::nullopt;
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
        
        response_.error = toCompactJson(e);
        response_.result = std::nullopt;  // Clear result if setting error
        return *this;
    }
    
    // Build the final response
    JsonRpcResponse build() const {
        return response_;
    }
    
    // Build and serialize to JSON string
    std::string buildJson() const {
        return toCompactJson(response_);
    }
    
    // Static helper for success response
    static JsonRpcResponse makeSuccess(int id, const std::string& result_json) {
        return JsonRpcResponseBuilder()
            .id(id)
            .resultJson(result_json)
            .build();
    }
    
    // Static helper for error response
    static JsonRpcResponse makeError(int id, int code, const std::string& message) {
        return JsonRpcResponseBuilder()
            .id(id)
            .error(code, message)
            .build();
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
    
    // Check if response is an error
    static bool isError(const JsonRpcResponse& response) {
        return response.error.has_value();
    }
    
    // Extract error details
    static bool getError(const JsonRpcResponse& response, ErrorInfo& error) {
        if (!response.error.has_value()) {
            return false;
        }
        return parseParams(*response.error, error);
    }
    
    // Extract result as a specific type
    template<typename T>
    static bool getResult(const JsonRpcResponse& response, T& result) {
        if (!response.result.has_value()) {
            return false;
        }
        return parseParams(*response.result, result);
    }
};

} // namespace jsonrpc