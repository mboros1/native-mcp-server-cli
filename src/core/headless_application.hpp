#ifndef HEADLESS_APPLICATION_HPP
#define HEADLESS_APPLICATION_HPP

#include <string>
#include <functional>
#include <memory>
#include <queue>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "../include/types_core.hpp"
#include "state_manager.hpp"
#include "input_handler.hpp"
#include "../network/mcp_client.hpp"

/**
 * HeadlessApplication provides a non-UI interface for testing and automation
 * 
 * Key features:
 * - Command processing without FTXUI dependencies
 * - Event-driven architecture with callbacks
 * - Synchronous and asynchronous operation modes
 * - Full access to application state for testing
 */
class HeadlessApplication {
public:
    // Event callbacks for testing
    using MessageCallback = std::function<void(LogEntryType type, const std::string& content)>;
    using StateChangeCallback = std::function<void(const std::string& state_type, const std::string& data)>;
    using ConnectionCallback = std::function<void(bool connected)>;
    using CommandCompleteCallback = std::function<void(bool success, const std::string& result)>;
    
    // Command result for synchronous operations
    struct CommandResult {
        bool success;
        std::string response;
        LogEntryType response_type;
        std::chrono::milliseconds elapsed_time;
    };

private:
    // Core components (UI-agnostic)
    Config config_;
    StateManager state_;
    std::unique_ptr<InputHandler> input_handler_;
    std::unique_ptr<MCPClient> mcp_client_;
    
    // Headless-specific
    bool running_ = false;
    std::thread event_loop_thread_;
    std::mutex event_mutex_;
    std::condition_variable event_cv_;
    std::queue<std::pair<std::string, CommandCompleteCallback>> command_queue_;
    
    // Callbacks
    MessageCallback on_message_;
    StateChangeCallback on_state_change_;
    ConnectionCallback on_connection_change_;
    
    // Testing helpers
    std::chrono::milliseconds command_timeout_{120000};  // Increased for agent mode
    
    // Message tracking for WaitForNextMessage
    std::mutex message_mutex_;
    std::condition_variable message_cv_;
    LogEntryType last_message_type_;
    bool message_received_ = false;
    
public:
    HeadlessApplication();
    ~HeadlessApplication();
    
    // === Initialization ===
    
    /**
     * Initialize the application with a config file
     */
    bool Initialize(const std::string& config_path);
    
    /**
     * Use mocked chat history instead of loading from file
     * Must be called before Initialize()
     */
    void UseMockedChatHistory(const std::vector<ChatHistoryEntry>& entries);
    
    /**
     * Connect to the MCP server
     */
    bool Connect(const std::string& host = "127.0.0.1", int port = 4000);
    
    /**
     * Start the event processing loop
     */
    void Start();
    
    /**
     * Stop the application gracefully
     */
    void Stop();
    
    // === Command Execution ===
    
    /**
     * Execute a command synchronously (blocks until response or timeout)
     */
    CommandResult ExecuteCommand(const std::string& command, 
                                 std::chrono::milliseconds timeout = std::chrono::milliseconds(120000));
    
    /**
     * Execute a command asynchronously with callback
     */
    void ExecuteCommandAsync(const std::string& command, CommandCompleteCallback callback);
    
    /**
     * Send a chat message and wait for response
     * Uses the default timeout from the CHAT_SEND procedure (120s)
     */
    CommandResult SendChatMessage(const std::string& message);
    
    // === Event Handling ===
    
    /**
     * Register callback for message events
     */
    void OnMessage(MessageCallback callback) { on_message_ = callback; }
    
    /**
     * Register callback for state changes
     */
    void OnStateChange(StateChangeCallback callback) { on_state_change_ = callback; }
    
    /**
     * Register callback for connection changes
     */
    void OnConnectionChange(ConnectionCallback callback) { on_connection_change_ = callback; }
    
    // === State Access (for testing) ===
    
    /**
     * Get current conversation log
     */
    std::vector<LogEntry> GetConversationLog() const;
    
    /**
     * Get current token count
     */
    size_t GetTokenCount() const { return state_.GetTotalTokens(); }
    
    /**
     * Check if awaiting response
     */
    bool IsAwaitingResponse() const { return state_.IsAwaitingResponse(); }
    
    /**
     * Get available chat histories
     */
    std::vector<std::pair<std::string, std::string>> GetChatHistories() const {
        return state_.GetAvailableChatHistories();
    }
    
    /**
     * Load a chat history by index
     */
    bool LoadChatHistory(size_t index);
    
    /**
     * Clear conversation
     */
    void ClearConversation();
    
    // === Testing Utilities ===
    
    /**
     * Simulate network disconnect
     */
    void SimulateDisconnect();
    
    /**
     * Simulate timeout
     */
    void SimulateTimeout();
    
    /**
     * Get current model settings
     */
    std::pair<std::string, std::string> GetModelSettings() const {
        return {input_handler_->model(), input_handler_->effort()};
    }
    
    /**
     * Set model
     */
    void SetModel(const std::string& model);
    
    /**
     * Set reasoning effort
     */
    void SetReasoningEffort(const std::string& effort);
    
    /**
     * Wait for a specific state or timeout
     */
    bool WaitForState(std::function<bool()> predicate, 
                      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    
    /**
     * Wait for the next message of a specific type with timeout
     * Returns true if message was received, false on timeout
     */
    bool WaitForNextMessage(LogEntryType expected_type,
                           std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    
    /**
     * Process all pending events (for testing)
     */
    void ProcessPendingEvents();
    
private:
    void EventLoop();
    void ProcessCommand(const std::string& command);
    void SetupCallbacks();
    void NotifyMessage(LogEntryType type, const std::string& content);
    void NotifyStateChange(const std::string& state_type, const std::string& data);
};

#endif // HEADLESS_APPLICATION_HPP