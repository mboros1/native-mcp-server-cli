#pragma once

#include "mock_tcp_server.hpp"
#include "../src/protocol/jsonrpc_messages.hpp"
#include <simdjson.h>
#include <spdlog/spdlog.h>
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
        SPDLOG_INFO("=== Mock server received request ===");
        SPDLOG_INFO("Raw request: {}", request_str);
        
        // Use simdjson to parse and extract method
        simdjson::dom::parser parser;
        simdjson::dom::element doc;
        
        auto error = parser.parse(request_str).get(doc);
        if (error) {
            SPDLOG_ERROR("Failed to parse JSON: {}", simdjson::error_message(error));
            return jsonrpc::JsonRpcResponseBuilder()
                .id(0)
                .error(-32700, "Parse error")
                .buildJson();
        }
        
        SPDLOG_INFO("Successfully parsed JSON");
        
        // Extract basic fields
        std::string method;
        std::optional<int> id;
        
        if (doc["method"].is_string()) {
            method = std::string(doc["method"].get_string().value());
            SPDLOG_INFO("Method: {}", method);
        } else {
            SPDLOG_WARN("No method field found in request");
            return jsonrpc::JsonRpcResponseBuilder()
                .id(0)
                .error(-32600, "Invalid Request - missing method")
                .buildJson();
        }
        
        if (doc["id"].is_int64()) {
            id = doc["id"].get_int64().value();
            SPDLOG_INFO("ID (int64): {}", id.value());
        } else if (doc["id"].is_uint64()) {
            id = static_cast<int>(doc["id"].get_uint64().value());
            SPDLOG_INFO("ID (uint64): {}", id.value());
        } else if (doc["id"].is_null()) {
            SPDLOG_INFO("ID is null (notification)");
        } else {
            SPDLOG_INFO("ID field not present or not a number");
        }
        
        // Check params
        if (!doc["params"].is_null()) {
            SPDLOG_INFO("Has params field");
            // Try to stringify the params
            std::string params_str = simdjson::minify(doc["params"]);
            SPDLOG_INFO("Params: {}", params_str);
        } else {
            SPDLOG_INFO("No params field");
        }
        
        // Update statistics
        stats_.total_requests++;
        stats_.method_counts[method]++;
        
        // Store simplified request for history
        jsonrpc::JsonRpcRequest req;
        req.method = method;
        req.id = id;
        stats_.request_history.push_back(req);
        
        // Check for notification (no response needed)
        if (!id.has_value()) {
            SPDLOG_INFO("This is a notification - no response will be sent");
            return "";  // No response for notifications
        }
        
        std::lock_guard<std::mutex> lock(response_mutex_);
        
        // 1. Check for next response (highest priority)
        if (!next_responses_.empty()) {
            MockResponse resp = next_responses_.front();
            next_responses_.pop();
            std::string response = BuildResponse(req, resp);
            SPDLOG_INFO("=== Mock server sending response ===");
            SPDLOG_INFO("Response: {}", response);
            return response;
        }
        
        // 2. Check for custom handler
        auto handler_it = custom_handlers_.find(method);
        if (handler_it != custom_handlers_.end()) {
            std::string response = handler_it->second(req);
            SPDLOG_INFO("=== Mock server sending response (custom handler) ===");
            SPDLOG_INFO("Response: {}", response);
            return response;
        }
        
        // 3. Check for queued method response
        auto queue_it = method_responses_.find(method);
        if (queue_it != method_responses_.end() && !queue_it->second.empty()) {
            MockResponse resp = queue_it->second.front();
            if (resp.once) {
                queue_it->second.pop();
            }
            std::string response = BuildResponse(req, resp);
            SPDLOG_INFO("=== Mock server sending response (queued) ===");
            SPDLOG_INFO("Response: {}", response);
            return response;
        }
        
        // 4. Check for default method response
        auto default_it = default_responses_.find(method);
        if (default_it != default_responses_.end()) {
            std::string response = BuildResponse(req, default_it->second);
            SPDLOG_INFO("=== Mock server sending response (default) ===");
            SPDLOG_INFO("Response: {}", response);
            return response;
        }
        
        // 5. Return method not found
        std::string error_response = jsonrpc::JsonRpcResponseBuilder()
            .id(id.value_or(0))
            .error(-32601, "Method not found")
            .buildJson();
        SPDLOG_INFO("=== Mock server sending error response (method not found) ===");
        SPDLOG_INFO("Response: {}", error_response);
        return error_response;
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