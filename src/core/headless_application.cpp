#include "headless_application.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <simdjson.h>

using namespace simdjson;

HeadlessApplication::HeadlessApplication() {
    // Initialize state manager with defaults
    state_.SetMaxHistorySize(100);
}

HeadlessApplication::~HeadlessApplication() {
    Stop();
}

bool HeadlessApplication::Initialize(const std::string& config_path) {
    SPDLOG_INFO("Initializing headless application with config: {}", config_path);
    
    // Load configuration
    std::ifstream file(config_path);
    if (!file.is_open()) {
        SPDLOG_ERROR("Failed to open config file: {}", config_path);
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();
    
    dom::parser parser;
    dom::element doc;
    
    auto error = parser.parse(json_str).get(doc);
    if (error) {
        SPDLOG_ERROR("Failed to parse config: {}", error_message(error));
        return false;
    }
    
    // Parse config into Config struct
    // (simplified - would need full parsing logic from Application::LoadConfig)
    if (doc["ui"]["welcomeMessage"].is_string()) {
        config_.welcomeMessage = std::string(doc["ui"]["welcomeMessage"].get_string().value());
    }
    if (doc["settings"]["maxHistorySize"].is_int64()) {
        config_.maxHistorySize = int(doc["settings"]["maxHistorySize"].get_int64().value());
    }
    
    // Initialize components
    input_handler_ = std::make_unique<InputHandler>(state_, config_.tools);
    
    // Load existing chat history
    state_.LoadChatHistoryOnStartup();
    
    // Setup internal callbacks
    SetupCallbacks();
    
    return true;
}

bool HeadlessApplication::Connect(const std::string& host, int port) {
    SPDLOG_INFO("Connecting to MCP server at {}:{}", host, port);
    
    mcp_client_ = std::make_unique<MCPClient>(host, port);
    
    // Set up callback to handle response IDs for JSON-RPC correlation
    mcp_client_->SetIdCallback([this](int id) {
      input_handler_->OnResponseReceived(id);
    });
    
    // Set up message callback
    mcp_client_->SetResponseCallback([this](const std::string& type, const std::string& content) {
        // Process server messages inline
        if (type == "RESPONSE") {
            state_.ClearAwaitingResponse();
            state_.WriteToChatHistory("assistant", content);
            NotifyMessage(LogEntryType::RESPONSE, content);
        } else if (type == "SYSTEM") {
            NotifyMessage(LogEntryType::SYSTEM, content);
        } else if (type == "ERROR") {
            state_.ClearAwaitingResponse();
            NotifyMessage(LogEntryType::ERROR, content);
        } else if (type == "TOOL_EVENT") {
            // Handle tool-related events (tool calls, results, errors, info)
            NotifyMessage(LogEntryType::SYSTEM, content);
        }
    });
    
    // Attempt connection
    if (!mcp_client_->Connect()) {
        SPDLOG_ERROR("Failed to connect to MCP server");
        if (on_connection_change_) {
            on_connection_change_(false);
        }
        return false;
    }
    
    // Set up input handler with MCP client
    input_handler_->SetCommMode(CommMode::IPC);
    input_handler_->SetMCPClient(mcp_client_.get());
    
    if (on_connection_change_) {
        on_connection_change_(true);
    }
    
    return true;
}

void HeadlessApplication::Start() {
    if (running_) return;
    
    running_ = true;
    event_loop_thread_ = std::thread([this]() { EventLoop(); });
    
    SPDLOG_INFO("Headless application started");
}

void HeadlessApplication::Stop() {
    if (!running_) return;
    
    running_ = false;
    event_cv_.notify_all();
    
    if (event_loop_thread_.joinable()) {
        event_loop_thread_.join();
    }
    
    if (mcp_client_) {
        // MCPClient doesn't have a Disconnect method, destructor handles cleanup
        mcp_client_.reset();
    }
    
    SPDLOG_INFO("Headless application stopped");
}

HeadlessApplication::CommandResult HeadlessApplication::ExecuteCommand(
    const std::string& command, 
    std::chrono::milliseconds timeout) {
    
    auto start_time = std::chrono::steady_clock::now();
    CommandResult result;
    result.success = false;
    
    // For synchronous execution, we need to capture the response
    std::mutex response_mutex;
    std::condition_variable response_cv;
    bool response_received = false;
    
    // Set up temporary callback to capture response
    auto original_callback = on_message_;
    on_message_ = [&](LogEntryType type, const std::string& content) {
        std::lock_guard<std::mutex> lock(response_mutex);
        result.response = content;
        result.response_type = type;
        result.success = (type != LogEntryType::ERROR);
        response_received = true;
        response_cv.notify_one();
        
        // Also call original callback if set
        if (original_callback) {
            original_callback(type, content);
        }
    };
    
    // Process the command
    ProcessCommand(command);
    
    // Wait for response or timeout
    std::unique_lock<std::mutex> lock(response_mutex);
    if (response_cv.wait_for(lock, timeout, [&]() { return response_received; })) {
        auto end_time = std::chrono::steady_clock::now();
        result.elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time);
    } else {
        result.success = false;
        result.response = "Command timed out";
        result.response_type = LogEntryType::ERROR;
        result.elapsed_time = timeout;
    }
    
    // Restore original callback
    on_message_ = original_callback;
    
    return result;
}

void HeadlessApplication::ExecuteCommandAsync(
    const std::string& command, 
    CommandCompleteCallback callback) {
    
    std::lock_guard<std::mutex> lock(event_mutex_);
    command_queue_.push({command, callback});
    event_cv_.notify_one();
}

HeadlessApplication::CommandResult HeadlessApplication::SendChatMessage(
    const std::string& message,
    std::chrono::milliseconds timeout) {
    
    // Chat messages are just commands without the slash
    return ExecuteCommand(message, timeout);
}

std::vector<LogEntry> HeadlessApplication::GetConversationLog() const {
    const auto& log = state_.GetEventLog();
    return std::vector<LogEntry>(log.begin(), log.end());
}

bool HeadlessApplication::LoadChatHistory(size_t index) {
    if (state_.LoadChatHistory(index)) {
        state_.ClearEventLog();
        state_.LoadChatHistoryOnStartup();
        NotifyMessage(LogEntryType::SYSTEM, "Loaded conversation #" + std::to_string(index));
        return true;
    }
    return false;
}

void HeadlessApplication::ClearConversation() {
    state_.ClearEventLog();
    NotifyMessage(LogEntryType::SYSTEM, "Conversation cleared");
}

void HeadlessApplication::SimulateDisconnect() {
    if (mcp_client_) {
        // Reset the client to simulate disconnect
        mcp_client_.reset();
        if (on_connection_change_) {
            on_connection_change_(false);
        }
    }
}

void HeadlessApplication::SimulateTimeout() {
    // Set a very short timeout for testing
    command_timeout_ = std::chrono::milliseconds(1);
}

void HeadlessApplication::SetModel(const std::string& model) {
    ProcessCommand("/model " + model);
}

void HeadlessApplication::SetReasoningEffort(const std::string& effort) {
    ProcessCommand("/think " + effort);
}

bool HeadlessApplication::WaitForState(
    std::function<bool()> predicate, 
    std::chrono::milliseconds timeout) {
    
    auto start = std::chrono::steady_clock::now();
    while (!predicate()) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        ProcessPendingEvents();
    }
    return true;
}

void HeadlessApplication::ProcessPendingEvents() {
    // Process any pending MCP client events
    // Note: MCPClient ProcessEvents is handled internally
    
    // Process command queue
    std::unique_lock<std::mutex> lock(event_mutex_);
    while (!command_queue_.empty()) {
        auto [command, callback] = command_queue_.front();
        command_queue_.pop();
        lock.unlock();
        
        ProcessCommand(command);
        
        if (callback) {
            // Get last log entry as result
            const auto& log = state_.GetEventLog();
            if (!log.empty()) {
                const auto& last_entry = log.back();
                callback(last_entry.type != LogEntryType::ERROR, last_entry.content);
            } else {
                callback(false, "No response");
            }
        }
        
        lock.lock();
    }
}

void HeadlessApplication::EventLoop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(event_mutex_);
        event_cv_.wait_for(lock, std::chrono::milliseconds(100));
        
        if (!running_) break;
        
        lock.unlock();
        ProcessPendingEvents();
    }
}

void HeadlessApplication::ProcessCommand(const std::string& command) {
    SPDLOG_DEBUG("Processing command in headless mode: {}", command);
    
    // Process the command through input handler
    input_handler_->ProcessCommand(command);
}

// ProcessServerMessage removed - logic moved inline to callback

void HeadlessApplication::SetupCallbacks() {
    // This would set up internal callbacks between components
    // Similar to what Application does but without UI updates
}

void HeadlessApplication::NotifyMessage(LogEntryType type, const std::string& content) {
    if (on_message_) {
        on_message_(type, content);
    }
}

void HeadlessApplication::NotifyStateChange(const std::string& state_type, const std::string& data) {
    if (on_state_change_) {
        on_state_change_(state_type, data);
    }
}