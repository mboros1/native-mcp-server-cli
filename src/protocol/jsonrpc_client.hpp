#pragma once

#include "jsonrpc_types.hpp"
#include <map>
#include <functional>
#include <mutex>
#include <future>
#include <spdlog/spdlog.h>

namespace jsonrpc {

// ============================================================================
// JSON-RPC Client Implementation
// ============================================================================

class JsonRpcClient {
public:
    using SendFunction = std::function<void(const std::string&)>;
    using NotificationHandler = std::function<void(const std::string& method, const std::string& params)>;
    
private:
    // Function to send raw JSON over the transport
    SendFunction send_fn_;
    
    // Track pending requests
    struct PendingRequest {
        std::promise<std::string> promise;
        std::chrono::steady_clock::time_point created;
        std::string method;
    };
    
    std::map<RequestId, PendingRequest> pending_requests_;
    std::mutex pending_mutex_;
    
    // Notification handlers
    std::map<std::string, NotificationHandler> notification_handlers_;
    std::mutex handlers_mutex_;
    
    // Session info
    std::string session_id_;
    std::vector<std::string> server_capabilities_;
    RateLimitInfo rate_limit_;
    
    // Statistics
    struct Stats {
        size_t requests_sent = 0;
        size_t responses_received = 0;
        size_t notifications_received = 0;
        size_t errors_received = 0;
        size_t timeouts = 0;
    } stats_;
    
public:
    JsonRpcClient(SendFunction send_fn) : send_fn_(send_fn) {}
    
    // ========================================================================
    // Request Methods - return futures for async responses
    // ========================================================================
    
    // Send a chat message
    std::future<ChatResponse> sendChat(const std::string& content,
                                       const std::string& model = "kimi",
                                       int timeout_ms = 120000,
                                       const std::string& reasoning_effort = "medium") {
        ChatRequest req = RequestBuilder::chat(content, model, timeout_ms);
        req.params.reasoning_effort = reasoning_effort;
        
        return sendRequestAsync<ChatRequest, ChatResponse>(req);
    }
    
    // Request sync check
    std::future<SyncResponse> requestSync() {
        SyncRequest req = RequestBuilder::sync();
        return sendRequestAsync<SyncRequest, SyncResponse>(req);
    }
    
    // Request reload
    std::future<SimpleResponse> requestReload() {
        ReloadRequest req = RequestBuilder::reload();
        return sendRequestAsync<ReloadRequest, SimpleResponse>(req);
    }
    
    // Get tool list
    std::future<ToolListResponse> getToolList() {
        ToolListRequest req = RequestBuilder::toolList();
        return sendRequestAsync<ToolListRequest, ToolListResponse>(req);
    }
    
    // Execute a tool
    std::future<ToolExecuteResponse> executeTool(const std::string& tool_name,
                                                 const std::string& args_json) {
        ToolExecuteRequest req;
        req.id = RequestBuilder::nextId();
        req.method = "tools.execute";
        req.params.name = tool_name;
        req.params.arguments = args_json;
        
        return sendRequestAsync<ToolExecuteRequest, ToolExecuteResponse>(req);
    }
    
    // Retry last message
    std::future<ChatResponse> retry(const std::string& original_message = "",
                                   const std::string& model = "kimi",
                                   const std::string& reasoning_effort = "medium") {
        RetryRequest req;
        req.id = RequestBuilder::nextId();
        req.method = "chat.retry";
        if (!original_message.empty()) {
            req.params.originalMessage = original_message;
        }
        req.params.model = model;
        req.params.reasoning_effort = reasoning_effort;
        
        return sendRequestAsync<RetryRequest, ChatResponse>(req);
    }
    
    // Cancel a request
    std::future<SimpleResponse> cancel(std::optional<RequestId> target_id = std::nullopt) {
        CancelRequest req = RequestBuilder::cancel(target_id);
        return sendRequestAsync<CancelRequest, SimpleResponse>(req);
    }
    
    // Version negotiation
    std::future<HelloResponse> hello(const std::string& client_version,
                                    const std::vector<std::string>& capabilities) {
        HelloRequest req = RequestBuilder::hello(client_version, capabilities);
        return sendRequestAsync<HelloRequest, HelloResponse>(req);
    }
    
    // ========================================================================
    // Transaction Support
    // ========================================================================
    
    std::future<TransactionBeginResponse> beginTransaction(const std::vector<std::string>& operations,
                                                          int timeout_ms = 300000) {
        TransactionBeginRequest req;
        req.id = RequestBuilder::nextId();
        req.method = "transaction.begin";
        req.params.operations = operations;
        req.params.timeout = timeout_ms;
        
        return sendRequestAsync<TransactionBeginRequest, TransactionBeginResponse>(req);
    }
    
    std::future<SimpleResponse> commitTransaction(const std::string& transaction_id) {
        TransactionEndRequest req;
        req.id = RequestBuilder::nextId();
        req.method = "transaction.commit";
        req.params.transactionId = transaction_id;
        
        return sendRequestAsync<TransactionEndRequest, SimpleResponse>(req);
    }
    
    std::future<SimpleResponse> rollbackTransaction(const std::string& transaction_id) {
        TransactionEndRequest req;
        req.id = RequestBuilder::nextId();
        req.method = "transaction.rollback";
        req.params.transactionId = transaction_id;
        
        return sendRequestAsync<TransactionEndRequest, SimpleResponse>(req);
    }
    
    // ========================================================================
    // Notification Handlers
    // ========================================================================
    
    void onNotification(const std::string& method, NotificationHandler handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        notification_handlers_[method] = handler;
    }
    
    // Register common notification handlers
    void registerDefaultHandlers() {
        // Tool called
        onNotification("tool.called", [this](const std::string& method, const std::string& params) {
            ToolCalledParams p;
            if (parseParams(params, p)) {
                handleToolCalled(p);
            }
        });
        
        // Tool result
        onNotification("tool.result", [this](const std::string& method, const std::string& params) {
            ToolResultParams p;
            if (parseParams(params, p)) {
                handleToolResult(p);
            }
        });
        
        // Stream chunk
        onNotification("stream.chunk", [this](const std::string& method, const std::string& params) {
            StreamChunkParams p;
            if (parseParams(params, p)) {
                handleStreamChunk(p);
            }
        });
        
        // Stream end
        onNotification("stream.end", [this](const std::string& method, const std::string& params) {
            StreamEndParams p;
            if (parseParams(params, p)) {
                handleStreamEnd(p);
            }
        });
        
        // Sync required
        onNotification("sync.required", [this](const std::string& method, const std::string& params) {
            SyncRequiredParams p;
            if (parseParams(params, p)) {
                handleSyncRequired(p);
            }
        });
    }
    
    // ========================================================================
    // Response Processing
    // ========================================================================
    
    void processResponse(const std::string& json) {
        SPDLOG_DEBUG("Processing JSON-RPC response: {}", json);
        
        // Check if this is a batch response
        if (json[0] == '[') {
            processBatchResponse(json);
            return;
        }
        
        // Parse to check if it's a notification or response
        if (json.find("\"id\"") == std::string::npos || 
            json.find("\"id\":null") != std::string::npos) {
            // This is a notification
            processNotification(json);
            return;
        }
        
        // Parse the response to get the ID
        Response base_response;
        if (!ResponseParser::parse(json, base_response)) {
            SPDLOG_ERROR("Failed to parse response: {}", json);
            return;
        }
        
        // Find the pending request
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_requests_.find(base_response.id);
        if (it == pending_requests_.end()) {
            SPDLOG_WARN("Received response for unknown request ID");
            return;
        }
        
        // Fulfill the promise
        it->second.promise.set_value(json);
        pending_requests_.erase(it);
        
        stats_.responses_received++;
        
        // Check if this is an error response
        if (ResponseParser::isError(json)) {
            stats_.errors_received++;
        }
    }
    
    // ========================================================================
    // Timeout Management
    // ========================================================================
    
    void checkTimeouts(std::chrono::seconds timeout_duration = std::chrono::seconds(300)) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto now = std::chrono::steady_clock::now();
        
        auto it = pending_requests_.begin();
        while (it != pending_requests_.end()) {
            if (now - it->second.created > timeout_duration) {
                SPDLOG_WARN("Request {} timed out (method: {})", 
                           "id", it->second.method);  // TODO: format ID properly
                
                // Create timeout error response
                ErrorResponse timeout_error;
                timeout_error.id = it->first;
                timeout_error.error.code = errorCodeToInt(ErrorCode::Timeout);
                timeout_error.error.message = "Request timed out";
                
                std::string error_json = JS::serializeStruct(timeout_error);
                it->second.promise.set_value(error_json);
                
                it = pending_requests_.erase(it);
                stats_.timeouts++;
            } else {
                ++it;
            }
        }
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    const Stats& getStats() const { return stats_; }
    
    void resetStats() {
        stats_ = Stats{};
    }
    
    size_t getPendingRequestCount() const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(pending_mutex_));
        return pending_requests_.size();
    }
    
    // ========================================================================
    // Session Management
    // ========================================================================
    
    const std::string& getSessionId() const { return session_id_; }
    const std::vector<std::string>& getServerCapabilities() const { return server_capabilities_; }
    const RateLimitInfo& getRateLimit() const { return rate_limit_; }
    
    void updateRateLimit(const RateLimitInfo& info) {
        rate_limit_ = info;
    }
    
private:
    // ========================================================================
    // Internal Helper Methods
    // ========================================================================
    
    template<typename TRequest, typename TResponse>
    std::future<TResponse> sendRequestAsync(const TRequest& request) {
        // Validate request
        if (!Validator::validateRequest(request)) {
            SPDLOG_ERROR("Invalid request: {}", request.method);
            std::promise<TResponse> error_promise;
            error_promise.set_exception(std::make_exception_ptr(
                std::runtime_error("Invalid request")));
            return error_promise.get_future();
        }
        
        // Serialize request
        std::string json = JS::serializeStruct(request);
        SPDLOG_DEBUG("Sending JSON-RPC request: {}", json);
        
        // Create promise for the response
        auto promise = std::make_shared<std::promise<TResponse>>();
        auto future = promise->get_future();
        
        // Store pending request
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            PendingRequest pending;
            pending.promise = std::promise<std::string>();
            pending.created = std::chrono::steady_clock::now();
            pending.method = request.method;
            
            auto string_future = pending.promise.get_future();
            pending_requests_[*request.id] = std::move(pending);
            
            // Set up continuation to parse the response
            std::thread([promise, string_future = std::move(string_future)]() mutable {
                try {
                    std::string response_json = string_future.get();
                    
                    // Check if error response
                    if (ResponseParser::isError(response_json)) {
                        ErrorResponse error_resp;
                        if (ResponseParser::parse(response_json, error_resp)) {
                            throw std::runtime_error(error_resp.error.message);
                        }
                    }
                    
                    // Parse success response
                    TResponse response;
                    if (!ResponseParser::parse(response_json, response)) {
                        throw std::runtime_error("Failed to parse response");
                    }
                    
                    promise->set_value(response);
                } catch (...) {
                    promise->set_exception(std::current_exception());
                }
            }).detach();
        }
        
        // Send the request
        send_fn_(json);
        stats_.requests_sent++;
        
        return future;
    }
    
    void processNotification(const std::string& json) {
        SPDLOG_DEBUG("Processing notification: {}", json);
        stats_.notifications_received++;
        
        // Parse to get method and params
        // This is simplified - real implementation would parse properly
        size_t method_pos = json.find("\"method\":\"");
        if (method_pos == std::string::npos) return;
        
        method_pos += 10;
        size_t method_end = json.find("\"", method_pos);
        std::string method = json.substr(method_pos, method_end - method_pos);
        
        // Extract params (simplified)
        size_t params_pos = json.find("\"params\":");
        if (params_pos == std::string::npos) return;
        
        params_pos += 9;
        size_t params_end = json.find_last_of("}");
        std::string params = json.substr(params_pos, params_end - params_pos);
        
        // Call handler if registered
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        auto it = notification_handlers_.find(method);
        if (it != notification_handlers_.end()) {
            it->second(method, params);
        } else {
            SPDLOG_DEBUG("No handler for notification: {}", method);
        }
    }
    
    void processBatchResponse(const std::string& json) {
        // TODO: Implement batch response processing
        SPDLOG_WARN("Batch responses not yet implemented");
    }
    
    template<typename T>
    bool parseParams(const std::string& json, T& params) {
        try {
            JS::ParseContext context(json);
            auto error = context.parseTo(params);
            return error == JS::Error::NoError;
        } catch (...) {
            return false;
        }
    }
    
    // Default notification handlers (can be overridden)
    virtual void handleToolCalled(const ToolCalledParams& params) {
        SPDLOG_INFO("Tool called: {}", params.tool_name);
    }
    
    virtual void handleToolResult(const ToolResultParams& params) {
        SPDLOG_INFO("Tool result: {} ({} items)", params.tool_name, params.total_items);
    }
    
    virtual void handleStreamChunk(const StreamChunkParams& params) {
        SPDLOG_DEBUG("Stream chunk {} for stream {}", params.index, params.streamId);
    }
    
    virtual void handleStreamEnd(const StreamEndParams& params) {
        SPDLOG_INFO("Stream {} ended with {} chunks", params.streamId, params.totalChunks);
    }
    
    virtual void handleSyncRequired(const SyncRequiredParams& params) {
        SPDLOG_WARN("Sync required: {} (action: {})", params.reason, params.action);
    }
};

} // namespace jsonrpc