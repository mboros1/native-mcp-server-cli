#pragma once

#include "mock_tcp_server.hpp"
#include "../src/protocol/jsonrpc_messages.hpp"
#include <queue>
#include <map>
#include <mutex>
#include <functional>
#include <variant>
#include <optional>
#include <thread>

/**
 * @brief Configurable Mock JSON-RPC Server for testing
 * 
 * Allows test writers to configure specific responses for specific requests.
 * Supports response queues, conditional responses, and custom handlers.
 */
class ConfigurableMockServer : public MockTcpServer {
public:
    // Response can be a string, an error, or a custom handler
    using ResponseHandler = std::function<std::string(const jsonrpc::JsonRpcRequest&)>;
    
    struct MockResponse {
        std::variant<std::string,              // Raw JSON response
                     jsonrpc::JsonRpcResponse,  // Structured response
                     jsonrpc::ErrorInfo,        // Error response
                     ResponseHandler            // Custom handler
                     > response;
        
        bool once = false;  // If true, remove after use
    };
    
    ConfigurableMockServer(unsigned short port = 0) 
        : MockTcpServer(port) {
        
        // Set up the main response handler
        SetResponseCallback([this](const std::string& request) {
            return ProcessRequest(request);
        });
        
        // Set up default handlers for basic methods
        SetupDefaultHandlers();
    }
    
    // ========================================================================
    // Configuration Methods for Test Writers
    // ========================================================================
    
    /**
     * @brief Queue a response for a specific method
     * @param method The JSON-RPC method name
     * @param response The response to return
     * @param once If true, use once then remove
     */
    void QueueMethodResponse(const std::string& method, const MockResponse& response) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        method_responses_[method].push(response);
    }
    
    /**
     * @brief Set a default response for a method (used when queue is empty)
     */
    void SetMethodDefault(const std::string& method, const MockResponse& response) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        default_responses_[method] = response;
    }
    
    /**
     * @brief Queue a one-time response for any next request
     */
    void QueueNextResponse(const MockResponse& response) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        next_responses_.push(response);
    }
    
    /**
     * @brief Set a custom handler for a specific method
     */
    void SetMethodHandler(const std::string& method, ResponseHandler handler) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        custom_handlers_[method] = handler;
    }
    
    /**
     * @brief Clear all configured responses
     */
    void ClearAllResponses() {
        std::lock_guard<std::mutex> lock(response_mutex_);
        method_responses_.clear();
        default_responses_.clear();
        while (!next_responses_.empty()) next_responses_.pop();
        custom_handlers_.clear();
    }
    
    // ========================================================================
    // Helper Methods for Common Scenarios
    // ========================================================================
    
    /**
     * @brief Configure to return success for a chat message
     */
    void RespondToChat(const std::string& reply, bool once = true) {
        jsonrpc::ChatResult result;
        result.reply = reply;
        result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        MockResponse mock_resp;
        mock_resp.response = [result](const jsonrpc::JsonRpcRequest& req) {
            return jsonrpc::JsonRpcResponseBuilder()
                .id(req.id.value_or(0))
                .result(result)
                .buildJson();
        };
        mock_resp.once = once;
        
        QueueMethodResponse("chat.send", mock_resp);
    }
    
    /**
     * @brief Configure to return an error for a method
     */
    void RespondWithError(const std::string& method, int code, const std::string& message) {
        MockResponse mock_resp;
        mock_resp.response = [code, message](const jsonrpc::JsonRpcRequest& req) {
            return jsonrpc::JsonRpcResponseBuilder()
                .id(req.id.value_or(0))
                .error(code, message)
                .buildJson();
        };
        
        SetMethodDefault(method, mock_resp);
    }
    
    /**
     * @brief Configure to timeout on next request
     */
    void SimulateTimeout() {
        MockResponse mock_resp;
        mock_resp.response = [](const jsonrpc::JsonRpcRequest& req) {
            // Delay to simulate timeout
            std::this_thread::sleep_for(std::chrono::seconds(130));
            return "";  // Never reached
        };
        mock_resp.once = true;
        
        QueueNextResponse(mock_resp);
    }
    
    /**
     * @brief Configure to return a list of tools
     */
    void RespondWithTools(const std::vector<std::string>& tools) {
        jsonrpc::ToolListResult result;
        result.tools = tools;
        result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        MockResponse mock_resp;
        mock_resp.response = [result](const jsonrpc::JsonRpcRequest& req) {
            return jsonrpc::JsonRpcResponseBuilder()
                .id(req.id.value_or(0))
                .result(result)
                .buildJson();
        };
        
        SetMethodDefault("tools.list", mock_resp);
    }
    
    // ========================================================================
    // Statistics
    // ========================================================================
    
    struct RequestStats {
        int total_requests = 0;
        std::map<std::string, int> method_counts;
        std::vector<jsonrpc::JsonRpcRequest> request_history;
    };
    
    const RequestStats& GetStats() const { return stats_; }
    void ResetStats() { stats_ = RequestStats{}; }
    
    /**
     * @brief Get the last request for a specific method
     */
    std::optional<jsonrpc::JsonRpcRequest> GetLastRequest(const std::string& method) {
        for (auto it = stats_.request_history.rbegin(); it != stats_.request_history.rend(); ++it) {
            if (it->method == method) {
                return *it;
            }
        }
        return std::nullopt;
    }
    
private:
    std::map<std::string, std::queue<MockResponse>> method_responses_;
    std::map<std::string, MockResponse> default_responses_;
    std::queue<MockResponse> next_responses_;
    std::map<std::string, ResponseHandler> custom_handlers_;
    mutable std::mutex response_mutex_;
    RequestStats stats_;
    
    /**
     * @brief Process an incoming request
     */
    std::string ProcessRequest(const std::string& request_str) {
        // Parse the request
        jsonrpc::JsonRpcRequest request;
        if (!jsonrpc::JsonRpcParser::parseRequest(request_str, request)) {
            // Invalid JSON
            return jsonrpc::JsonRpcResponseBuilder()
                .id(0)
                .error(-32700, "Parse error")
                .buildJson();
        }
        
        // Update statistics
        stats_.total_requests++;
        stats_.method_counts[request.method]++;
        stats_.request_history.push_back(request);
        
        // Check for notification (no response needed)
        if (!request.id.has_value()) {
            return "";  // No response for notifications
        }
        
        std::lock_guard<std::mutex> lock(response_mutex_);
        
        // 1. Check for next response (highest priority)
        if (!next_responses_.empty()) {
            MockResponse resp = next_responses_.front();
            next_responses_.pop();
            return BuildResponse(request, resp);
        }
        
        // 2. Check for custom handler
        auto handler_it = custom_handlers_.find(request.method);
        if (handler_it != custom_handlers_.end()) {
            return handler_it->second(request);
        }
        
        // 3. Check for queued method response
        auto queue_it = method_responses_.find(request.method);
        if (queue_it != method_responses_.end() && !queue_it->second.empty()) {
            MockResponse resp = queue_it->second.front();
            if (resp.once) {
                queue_it->second.pop();
            }
            return BuildResponse(request, resp);
        }
        
        // 4. Check for default method response
        auto default_it = default_responses_.find(request.method);
        if (default_it != default_responses_.end()) {
            return BuildResponse(request, default_it->second);
        }
        
        // 5. Return method not found
        return jsonrpc::JsonRpcResponseBuilder()
            .id(request.id.value_or(0))
            .error(-32601, "Method not found")
            .buildJson();
    }
    
    /**
     * @brief Build a response from a MockResponse
     */
    std::string BuildResponse(const jsonrpc::JsonRpcRequest& request, const MockResponse& mock_resp) {
        return std::visit([&request](auto&& arg) -> std::string {
            using T = std::decay_t<decltype(arg)>;
            
            if constexpr (std::is_same_v<T, std::string>) {
                // Raw JSON string
                return arg;
            } else if constexpr (std::is_same_v<T, jsonrpc::JsonRpcResponse>) {
                // Structured response - update ID and serialize
                auto resp = arg;
                resp.id = request.id.value_or(0);
                return jsonrpc::toCompactJson(resp);
            } else if constexpr (std::is_same_v<T, jsonrpc::ErrorInfo>) {
                // Error response
                return jsonrpc::JsonRpcResponseBuilder()
                    .id(request.id.value_or(0))
                    .error(arg.code, arg.message, arg.data.value_or(""))
                    .buildJson();
            } else if constexpr (std::is_same_v<T, ResponseHandler>) {
                // Custom handler
                return arg(request);
            }
            
            return "";
        }, mock_resp.response);
    }
    
    /**
     * @brief Set up default handlers for basic methods
     */
    void SetupDefaultHandlers() {
        // Default hello response
        SetMethodHandler("rpc.hello", [](const jsonrpc::JsonRpcRequest& req) {
            jsonrpc::HelloResult result;
            result.version = "2.0";
            result.serverVersion = "1.0.0-mock";
            result.capabilities = {"batch", "streaming", "tools"};
            result.sessionId = "mock-session";
            
            return jsonrpc::JsonRpcResponseBuilder()
                .id(req.id.value_or(0))
                .result(result)
                .buildJson();
        });
        
        // Default sync response
        SetMethodHandler("history.sync", [](const jsonrpc::JsonRpcRequest& req) {
            jsonrpc::SyncResult result;
            result.entries = 0;
            result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
            
            return jsonrpc::JsonRpcResponseBuilder()
                .id(req.id.value_or(0))
                .result(result)
                .buildJson();
        });
    }
};