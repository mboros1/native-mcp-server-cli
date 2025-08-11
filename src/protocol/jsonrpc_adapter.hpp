#pragma once

#include "jsonrpc_client.hpp"
#include "../network/mcp_client.hpp"
#include "../core/state_manager.hpp"
#include <spdlog/spdlog.h>

namespace jsonrpc {

// ============================================================================
// Adapter to integrate JSON-RPC client with existing MCP infrastructure
// ============================================================================

class JsonRpcAdapter {
private:
    JsonRpcClient client_;
    MCPClient* mcp_client_;
    StateManager* state_manager_;
    
    // Track if we're using JSON-RPC or legacy protocol
    bool use_jsonrpc_ = false;
    bool server_supports_jsonrpc_ = false;
    
public:
    JsonRpcAdapter(MCPClient* mcp_client, StateManager* state_manager) 
        : client_([mcp_client](const std::string& json) {
              if (mcp_client && mcp_client->IsConnected()) {
                  mcp_client->SendRequest(json);
              }
          }),
          mcp_client_(mcp_client),
          state_manager_(state_manager) {
        
        // Register notification handlers
        setupNotificationHandlers();
    }
    
    // ========================================================================
    // Protocol Detection and Negotiation
    // ========================================================================
    
    void negotiateProtocol() {
        if (!mcp_client_ || !mcp_client_->IsConnected()) {
            SPDLOG_WARN("Cannot negotiate protocol - not connected");
            return;
        }
        
        // Try JSON-RPC 2.0 hello
        auto future = client_.hello("1.0.0", 
            {"batch", "streaming", "tools", "transactions"});
        
        // Wait with timeout
        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
            try {
                auto response = future.get();
                SPDLOG_INFO("Server supports JSON-RPC 2.0: version={}, capabilities={}", 
                           response.result.serverVersion,
                           fmt::join(response.result.capabilities, ", "));
                
                use_jsonrpc_ = true;
                server_supports_jsonrpc_ = true;
                
                // Update rate limit if provided
                if (response.result.rateLimit.has_value()) {
                    client_.updateRateLimit(*response.result.rateLimit);
                }
            } catch (const std::exception& e) {
                SPDLOG_INFO("Server doesn't support JSON-RPC 2.0, using legacy protocol");
                use_jsonrpc_ = false;
            }
        } else {
            SPDLOG_INFO("Protocol negotiation timed out, using legacy protocol");
            use_jsonrpc_ = false;
        }
    }
    
    // ========================================================================
    // Send Methods - automatically choose protocol
    // ========================================================================
    
    void sendChat(const std::string& content, const ChatConfig& config, int timeout_ms) {
        if (use_jsonrpc_) {
            // Use JSON-RPC client
            auto future = client_.sendChat(content, config.model, timeout_ms, config.reasoning_effort);
            
            // Handle async response
            std::thread([this, future = std::move(future)]() mutable {
                try {
                    auto response = future.get();
                    handleChatResponse(response);
                } catch (const std::exception& e) {
                    handleChatError(e.what());
                }
            }).detach();
        } else {
            // Use legacy format
            sendLegacyChat(content, config, timeout_ms);
        }
    }
    
    void sendSync() {
        if (use_jsonrpc_) {
            auto future = client_.requestSync();
            
            std::thread([this, future = std::move(future)]() mutable {
                try {
                    auto response = future.get();
                    handleSyncResponse(response);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("Sync failed: {}", e.what());
                }
            }).detach();
        } else {
            sendLegacySync();
        }
    }
    
    void sendReload() {
        if (use_jsonrpc_) {
            auto future = client_.requestReload();
            
            std::thread([this, future = std::move(future)]() mutable {
                try {
                    auto response = future.get();
                    SPDLOG_INFO("Reload complete: {}", 
                               response.result.message.value_or("success"));
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("Reload failed: {}", e.what());
                }
            }).detach();
        } else {
            sendLegacyReload();
        }
    }
    
    void sendToolList() {
        if (use_jsonrpc_) {
            auto future = client_.getToolList();
            
            std::thread([this, future = std::move(future)]() mutable {
                try {
                    auto response = future.get();
                    handleToolListResponse(response);
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("Tool list failed: {}", e.what());
                }
            }).detach();
        } else {
            sendLegacyToolList();
        }
    }
    
    void sendRetry(const std::string& original_message, const ChatConfig& config) {
        if (use_jsonrpc_) {
            auto future = client_.retry(original_message, config.model, config.reasoning_effort);
            
            std::thread([this, future = std::move(future)]() mutable {
                try {
                    auto response = future.get();
                    handleChatResponse(response);
                } catch (const std::exception& e) {
                    handleChatError(e.what());
                }
            }).detach();
        } else {
            sendLegacyRetry(original_message, config);
        }
    }
    
    void sendCancel() {
        if (use_jsonrpc_) {
            auto future = client_.cancel();
            
            std::thread([this, future = std::move(future)]() mutable {
                try {
                    auto response = future.get();
                    SPDLOG_INFO("Cancel complete: {}", 
                               response.result.message.value_or("success"));
                } catch (const std::exception& e) {
                    SPDLOG_ERROR("Cancel failed: {}", e.what());
                }
            }).detach();
        } else {
            sendLegacyCancel();
        }
    }
    
    // ========================================================================
    // Process incoming messages
    // ========================================================================
    
    void processMessage(const std::string& json) {
        // Check if this is JSON-RPC format
        if (json.find("\"jsonrpc\":\"2.0\"") != std::string::npos) {
            // Process as JSON-RPC
            client_.processResponse(json);
        } else {
            // Process as legacy format
            processLegacyMessage(json);
        }
    }
    
    // ========================================================================
    // Statistics and Management
    // ========================================================================
    
    bool isUsingJsonRpc() const { return use_jsonrpc_; }
    bool serverSupportsJsonRpc() const { return server_supports_jsonrpc_; }
    
    const jsonrpc::JsonRpcClient::Stats& getStats() const { 
        return client_.getStats(); 
    }
    
    size_t getPendingRequestCount() const {
        return client_.getPendingRequestCount();
    }
    
    void checkTimeouts() {
        client_.checkTimeouts();
    }
    
private:
    // ========================================================================
    // Notification Handlers
    // ========================================================================
    
    void setupNotificationHandlers() {
        // Tool called
        client_.onNotification("tool.called", 
            [this](const std::string& method, const std::string& params_json) {
                ToolCalledParams params;
                JS::ParseContext context(params_json);
                if (context.parseTo(params) == JS::Error::NoError) {
                    std::string msg = fmt::format("🔧 Tool call: {} with args: {}", 
                                                 params.tool_name,
                                                 params.arguments.value_or("{}"));
                    state_manager_->AddLogEntry(LogEntryType::SYSTEM, msg);
                    state_manager_->WriteToChatHistory("system", msg);
                }
            });
        
        // Tool result preview
        client_.onNotification("tool.result",
            [this](const std::string& method, const std::string& params_json) {
                ToolResultParams params;
                JS::ParseContext context(params_json);
                if (context.parseTo(params) == JS::Error::NoError) {
                    std::string msg = fmt::format("📊 Tool result: {} ({} items)", 
                                                 params.tool_name,
                                                 params.total_items);
                    state_manager_->AddLogEntry(LogEntryType::SYSTEM, msg);
                }
            });
        
        // Sync required
        client_.onNotification("sync.required",
            [this](const std::string& method, const std::string& params_json) {
                SyncRequiredParams params;
                JS::ParseContext context(params_json);
                if (context.parseTo(params) == JS::Error::NoError) {
                    SPDLOG_WARN("Server requires sync: {} ({})", 
                               params.reason, params.action);
                    
                    if (params.action == "reload") {
                        // Automatically reload if requested
                        sendReload();
                    }
                }
            });
        
        // Stream chunk
        client_.onNotification("stream.chunk",
            [this](const std::string& method, const std::string& params_json) {
                StreamChunkParams params;
                JS::ParseContext context(params_json);
                if (context.parseTo(params) == JS::Error::NoError) {
                    // Append to response being built
                    state_manager_->AppendStreamChunk(params.streamId, params.chunk);
                }
            });
        
        // Stream end
        client_.onNotification("stream.end",
            [this](const std::string& method, const std::string& params_json) {
                StreamEndParams params;
                JS::ParseContext context(params_json);
                if (context.parseTo(params) == JS::Error::NoError) {
                    // Finalize streamed response
                    state_manager_->FinalizeStream(params.streamId);
                }
            });
    }
    
    // ========================================================================
    // Response Handlers
    // ========================================================================
    
    void handleChatResponse(const ChatResponse& response) {
        // Clear awaiting response
        state_manager_->ClearAwaitingResponse();
        
        // Add to chat history
        state_manager_->WriteToChatHistory("assistant", response.result.reply);
        
        // Add to event log
        state_manager_->AddLogEntry(LogEntryType::RESPONSE, response.result.reply);
        
        // Check for streaming
        if (response.result.streaming.has_value() && *response.result.streaming) {
            SPDLOG_INFO("Response will be streamed with ID: {}", 
                       response.result.streamId.value_or("unknown"));
        }
        
        // Update rate limit if provided
        if (response.meta.has_value() && response.meta->rateLimit.has_value()) {
            client_.updateRateLimit(*response.meta->rateLimit);
            SPDLOG_DEBUG("Rate limit updated: {}/{} (reset: {})",
                        response.meta->rateLimit->remaining,
                        response.meta->rateLimit->limit,
                        response.meta->rateLimit->reset);
        }
    }
    
    void handleChatError(const std::string& error) {
        state_manager_->ClearAwaitingResponse();
        state_manager_->AddLogEntry(LogEntryType::ERROR, "Error: " + error);
        
        // Check if this was a timeout
        if (error.find("timeout") != std::string::npos || 
            error.find("Timeout") != std::string::npos) {
            state_manager_->SetRetryAvailable(true, 
                state_manager_->GetAwaitingResponseMessage());
        }
    }
    
    void handleSyncResponse(const SyncResponse& response) {
        std::string msg = fmt::format("Server has {} chat entries", 
                                     response.result.entries);
        state_manager_->AddLogEntry(LogEntryType::SYSTEM, msg);
    }
    
    void handleToolListResponse(const ToolListResponse& response) {
        std::string msg = fmt::format("Available tools: {}", 
                                     fmt::join(response.result.tools, ", "));
        state_manager_->AddLogEntry(LogEntryType::SYSTEM, msg);
    }
    
    // ========================================================================
    // Legacy Protocol Methods (backward compatibility)
    // ========================================================================
    
    void sendLegacyChat(const std::string& content, const ChatConfig& config, int timeout_ms) {
        ChatRequest req;
        req.content = content;
        req.model = config.model;
        req.reasoning_effort = config.reasoning_effort;
        req.timeout = timeout_ms;
        
        std::string json = JS::serializeStruct(req);
        mcp_client_->SendRequest(json);
    }
    
    void sendLegacySync() {
        std::string json = R"({"type": "sync", "content": "request_history"})";
        mcp_client_->SendRequest(json);
    }
    
    void sendLegacyReload() {
        std::string json = R"({"type": "reload", "content": "chat_history"})";
        mcp_client_->SendRequest(json);
    }
    
    void sendLegacyToolList() {
        std::string json = R"({"type": "tool_list"})";
        mcp_client_->SendRequest(json);
    }
    
    void sendLegacyRetry(const std::string& original_message, const ChatConfig& config) {
        RetryRequest req;
        req.originalMessage = original_message;
        req.model = config.model;
        req.reasoning_effort = config.reasoning_effort;
        
        std::string json = JS::serializeStruct(req);
        mcp_client_->SendRequest(json);
    }
    
    void sendLegacyCancel() {
        std::string json = R"({"type": "reset"})";
        mcp_client_->SendRequest(json);
    }
    
    void processLegacyMessage(const std::string& json) {
        // This would handle the current format
        // The existing mcp_client.cpp logic would go here
        SPDLOG_DEBUG("Processing legacy message format");
    }
};

} // namespace jsonrpc