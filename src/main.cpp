#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/chrono.h>

#include "event.hpp"
#include "tcp_client.hpp"
#include "include/types.hpp"
#include "network/mcp_client.hpp"
#include "core/state_manager.hpp"
#include <atomic_queue.h>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/terminal.hpp>
#include <simdjson.h>
#include <json_struct.h>
#include <token_est.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <chrono>
#include <vector>
#include <atomic>
#include <map>
#include <thread>
#include <iomanip>
#include <deque>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <set>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <filesystem>
#include <iomanip>
#include <algorithm>

using namespace ftxui;
using namespace simdjson;

// Forward declarations
class Application;
class UIRenderer;
class InputHandler;
class StateManager;


// ============================================================================
// State Manager - Single source of truth for application state
// ============================================================================

// ============================================================================
// ConversationLog Manager - Handles scroll state for conversation display
// ============================================================================
class ConversationLogManager {
private:
  ScreenInteractive* screen_ = nullptr;

public:
  float scroll_y_ = 1.0f;  // Start at bottom (1.0 = 100% scrolled down)
  bool auto_scroll_ = true; // Auto-scroll to bottom on new messages
  
  void SetScreen(ScreenInteractive* screen) {
    screen_ = screen;
  }
  
  void OnNewMessage() {
    // Called when a new message is added
    if (auto_scroll_) {
      scroll_y_ = 1.0f;  // Scroll to bottom
    }
    
    // Trigger screen refresh to immediately show new message
    if (screen_) {
      screen_->Post(Event::Custom);
    }
  }
  
  bool HandleScrollEvent(Event event) {
    // Handle mouse wheel scrolling
    if (event.is_mouse() && event.mouse().button == Mouse::WheelUp) {
      scroll_y_ = std::max(0.0f, scroll_y_ - 0.1f);  // Scroll up
      auto_scroll_ = (scroll_y_ >= 0.99f);  // Re-enable auto-scroll if at bottom
      return true;
    }
    
    if (event.is_mouse() && event.mouse().button == Mouse::WheelDown) {
      scroll_y_ = std::min(1.0f, scroll_y_ + 0.1f);  // Scroll down
      auto_scroll_ = (scroll_y_ >= 0.99f);  // Re-enable auto-scroll if at bottom
      return true;
    }
    
    // Handle keyboard scrolling
    if (event == Event::ArrowUp || event == Event::PageUp) {
      scroll_y_ = std::max(0.0f, scroll_y_ - 0.1f);
      auto_scroll_ = (scroll_y_ >= 0.99f);
      return true;
    }
    
    if (event == Event::ArrowDown || event == Event::PageDown) {
      scroll_y_ = std::min(1.0f, scroll_y_ + 0.1f);
      auto_scroll_ = (scroll_y_ >= 0.99f);
      return true;
    }
    
    if (event == Event::Home) {
      scroll_y_ = 0.0f;
      auto_scroll_ = false;
      return true;
    }
    
    if (event == Event::End) {
      scroll_y_ = 1.0f;
      auto_scroll_ = true;
      return true;
    }
    
    return false;
  }
};

// ============================================================================
// InputWithHistory Component - Input field with command history navigation
// ============================================================================
class InputWithHistory : public ComponentBase {
private:
  std::deque<std::string> command_history_;
  std::string current_input_;
  int history_index_ = -1;  // -1 means not browsing history
  std::string* input_content_;
  Component input_component_;
  InputOption input_options_;
  
public:
  InputWithHistory(std::string* content, InputOption options) 
    : input_content_(content), input_options_(options) {
    input_component_ = Input(input_content_, input_options_);
    Add(input_component_);
    LoadHistory();
  }
  
  void AddToHistory(const std::string& command) {
    if (!command.empty() && (command_history_.empty() || command_history_.back() != command)) {
      command_history_.push_back(command);
      
      // Limit history size
      if (command_history_.size() > 100) {
        command_history_.pop_front();
      }
      
      // Save to persistent storage
      SaveHistory();
    }
    history_index_ = -1;  // Reset history browsing
  }
  
  bool OnEvent(Event event) override {
    // Only handle arrows when this input is focused and not in multiline mode at cursor positions that would conflict
    if (Focused()) {
      if (event == Event::ArrowUp) {
        NavigateHistoryUp();
        return true;
      }
      
      if (event == Event::ArrowDown) {
        NavigateHistoryDown();
        return true;
      }
    }
    
    // Let input component handle other events (including Ctrl+N)
    return ComponentBase::OnEvent(event);
  }
  
  // Expose the underlying input component for external access
  Component GetInputComponent() { return input_component_; }
  
private:
  void NavigateHistoryUp() {
    if (command_history_.empty()) return;
    
    // First time browsing history - save current input
    if (history_index_ == -1) {
      current_input_ = *input_content_;
      history_index_ = command_history_.size() - 1;
    } else if (history_index_ > 0) {
      history_index_--;
    }
    
    *input_content_ = command_history_[history_index_];
  }
  
  void NavigateHistoryDown() {
    if (history_index_ == -1) return;  // Not browsing history
    
    history_index_++;
    
    if (history_index_ >= (int)command_history_.size()) {
      // Restore original input
      *input_content_ = current_input_;
      history_index_ = -1;
    } else {
      *input_content_ = command_history_[history_index_];
    }
  }
  
  void LoadHistory() {
    // Create .data directory if it doesn't exist
    std::filesystem::create_directories(".data");
    
    std::ifstream history_file(".data/history.log");
    if (!history_file.is_open()) {
      return;  // File doesn't exist yet, that's ok
    }
    
    std::string line;
    while (std::getline(history_file, line)) {
      if (!line.empty()) {
        // Unescape newlines
        std::string unescaped = UnescapeString(line);
        command_history_.push_back(unescaped);
      }
    }
    
    // Enforce size limit after loading
    while (command_history_.size() > 100) {
      command_history_.pop_front();
    }
    
    history_file.close();
  }
  
  void SaveHistory() {
    // Create .data directory if it doesn't exist
    std::filesystem::create_directories(".data");
    
    std::ofstream history_file(".data/history.log");
    if (!history_file.is_open()) {
      SPDLOG_ERROR("Failed to open history file for writing");
      return;
    }
    
    for (const auto& command : command_history_) {
      // Escape newlines and write to file
      std::string escaped = EscapeString(command);
      history_file << escaped << "\n";
    }
    
    history_file.close();
  }
  
  std::string EscapeString(const std::string& str) {
    std::string result;
    for (char c : str) {
      switch (c) {
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\\': result += "\\\\"; break;
        default: result += c; break;
      }
    }
    return result;
  }
  
  std::string UnescapeString(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); i++) {
      if (str[i] == '\\' && i + 1 < str.length()) {
        switch (str[i + 1]) {
          case 'n': result += '\n'; i++; break;
          case 'r': result += '\r'; i++; break;
          case 't': result += '\t'; i++; break;
          case '\\': result += '\\'; i++; break;
          default: result += str[i]; break;
        }
      } else {
        result += str[i];
      }
    }
    return result;
  }
};

// ============================================================================
// Input Handler - Processes user input and updates state
// ============================================================================
class InputHandler {
public:
  const std::string& model()  const { return config_.model;  }
  const std::string& effort() const { return config_.reasoning_effort; }
private:
  ChatConfig config_;  // Configuration loaded from/saved to disk
  StateManager& state_;
  std::vector<Tool>& tools_;
  UIRenderer* renderer_ = nullptr;
  ConversationLogManager* log_manager_ = nullptr;
  CommMode comm_mode_ = CommMode::STANDALONE;
  MCPClient* mcp_client_ = nullptr;
  int message_id_ = 0;
  
  Tool* FindTool(const std::string& name) {
    for (auto& tool : tools_) {
      if (tool.name == name) {
        return &tool;
      }
    }
    return nullptr;
  }
  
  void DumpScreen();
  
public:
  bool sync_retry_enabled_ = true;  // Track retry state for sync operations
  
  void RequestSyncCheck(bool retry = true) {
    // Store retry state for the sync response handler
    sync_retry_enabled_ = retry;
    
    if (comm_mode_ == CommMode::IPC && mcp_client_ && mcp_client_->IsConnected()) {
      if (retry) {
        std::string request = R"({"type": "sync", "content": "request_history"})";
        mcp_client_->SendRequest(request);
        AddLogEntryWithNotification(LogEntryType::SYSTEM, "Requesting chat history sync check...");
        SPDLOG_DEBUG("Sent sync request to server (retry={})", retry);
      } else {
        // On retry attempt, first send reload request
        std::string reload_request = R"({"type": "reload", "content": "chat_history"})";
        mcp_client_->SendRequest(reload_request);
        AddLogEntryWithNotification(LogEntryType::SYSTEM, "Server out of sync - requesting reload and retry...");
        SPDLOG_DEBUG("Sent reload request, will sync again");
        
        // Small delay then send sync request
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::string sync_request = R"({"type": "sync", "content": "request_history"})";
        mcp_client_->SendRequest(sync_request);
      }
    } else {
      AddLogEntryWithNotification(LogEntryType::ERROR, "Sync command requires connection to MCP server");
    }
  }
  
  void AddLogEntryWithNotification(LogEntryType type, const std::string& content) {
    state_.AddLogEntry(type, content);
    if (log_manager_) {
      log_manager_->OnNewMessage();
    }
  }
private:

public:
  InputHandler(StateManager& state, std::vector<Tool>& tools) 
    : state_(state), tools_(tools) {
    LoadConfig();  // Load configuration on startup
  }
    
  void SetRenderer(UIRenderer* renderer) {
    renderer_ = renderer;
  }
  
  void SetLogManager(ConversationLogManager* manager) {
    log_manager_ = manager;
  }
  
  void LoadConfig() {
    std::filesystem::path config_path = std::filesystem::path(".config") / "chat-config.json";
    
    if (std::filesystem::exists(config_path)) {
      try {
        std::ifstream file(config_path);
        if (file.is_open()) {
          std::string json_str((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
          
          JS::ParseContext context(json_str);
          context.parseTo(config_);
          
          SPDLOG_INFO("Loaded configuration from {}: model={}, effort={}", 
                      config_path.string(), config_.model, config_.reasoning_effort);
        }
      } catch (const std::exception& e) {
        SPDLOG_ERROR("Failed to load config: {}", e.what());
        // Use defaults on error
      }
    } else {
      SPDLOG_INFO("No config file found at {}, using defaults", config_path.string());
      // Create .config directory if it doesn't exist
      std::filesystem::create_directories(config_path.parent_path());
      SaveConfig();  // Save default config
    }
  }
  
  void SaveConfig() {
    std::filesystem::path config_path = std::filesystem::path(".config") / "chat-config.json";
    
    try {
      // Ensure directory exists
      std::filesystem::create_directories(config_path.parent_path());
      
      // Serialize config to JSON
      std::string json = JS::serializeStruct(config_);
      
      // Write to file
      std::ofstream file(config_path);
      if (file.is_open()) {
        file << json;
        file.close();
        SPDLOG_INFO("Saved configuration to {}: model={}, effort={}", 
                    config_path.string(), config_.model, config_.reasoning_effort);
      } else {
        SPDLOG_ERROR("Failed to open config file for writing: {}", config_path.string());
      }
    } catch (const std::exception& e) {
      SPDLOG_ERROR("Failed to save config: {}", e.what());
    }
  }
  
  void SetCommMode(CommMode mode) {
    comm_mode_ = mode;
  }
  
  void SetMCPClient(MCPClient* client) {
    mcp_client_ = client;
  }
  
  void SendInterrupt() {
    if (!state_.IsAwaitingResponse()) {
      SPDLOG_DEBUG("No active request to interrupt");
      return;
    }
    
    if (comm_mode_ != CommMode::IPC || !mcp_client_ || !mcp_client_->IsConnected()) {
      AddLogEntryWithNotification(LogEntryType::ERROR, "Cannot interrupt - not connected to server");
      return;
    }
    
    SPDLOG_INFO("Sending interrupt request");
    
    // Build strongly-typed interrupt request
    InterruptRequest request;
    
    // Serialize to JSON using json_struct
    std::string json = JS::serializeStruct(request, JS::SerializerOptions(JS::SerializerOptions::Compact));
    mcp_client_->SendRequest(json);
    
    // Clear awaiting response state immediately
    state_.ClearAwaitingResponse();
    AddLogEntryWithNotification(LogEntryType::SYSTEM, "Interrupt sent - cancelling request");
  }

  void SendRetryRequest() {
    if (!state_.IsRetryAvailable()) {
      SPDLOG_WARN("No retry available");
      return;
    }
    
    if (comm_mode_ != CommMode::IPC || !mcp_client_ || !mcp_client_->IsConnected()) {
      AddLogEntryWithNotification(LogEntryType::ERROR, "Cannot retry - not connected to server");
      return;
    }
    
    std::string original_message = state_.GetRetryOriginalMessage();
    SPDLOG_INFO("Sending retry request for message: {}", original_message);
    
    // Build strongly-typed retry request
    RetryRequest request;
    request.originalMessage = original_message;
    request.model = config_.model;
    request.reasoning_effort = config_.reasoning_effort;
    
    // Serialize to JSON using json_struct
    std::string json = JS::serializeStruct(request, JS::SerializerOptions(JS::SerializerOptions::Compact));
    mcp_client_->SendRequest(json);
    
    // Clear retry state and add log entry
    state_.ClearRetry();
    AddLogEntryWithNotification(LogEntryType::SYSTEM, "Retrying: " + original_message);
    AddLogEntryWithNotification(LogEntryType::SYSTEM, "[Awaiting response...]");
  }
  
  void SendChatMessage(const std::string& message) {
    SPDLOG_INFO("SendChatMessage called with: {}", message);
    
    if (comm_mode_ != CommMode::IPC) {
      SPDLOG_WARN("Not in IPC mode, skipping");
      return;
    }
    
    if (!mcp_client_) {
      SPDLOG_ERROR("MCP client is null!");
      return;
    }
    
    // Check if already awaiting response - if so, block and add BLOCKED entry
    if (state_.IsAwaitingResponse()) {
      SPDLOG_WARN("Already awaiting response, blocking new message");
      AddLogEntryWithNotification(LogEntryType::BLOCKED, 
        "Message blocked - previous request still processing. Please wait or use retry if timed out.");
      return;
    }
    
    // Set awaiting response state
    state_.SetAwaitingResponse(message);
    
    // Build strongly-typed request
    ChatRequest request;
    request.id = ++message_id_;
    request.content = message;
    request.model = config_.model;
    request.reasoning_effort = config_.reasoning_effort;
    request.timeout = state_.GetServerTimeout().count();
    
    // Serialize to JSON using json_struct
    std::string json = JS::serializeStruct(request, JS::SerializerOptions(JS::SerializerOptions::Compact));
    
    SPDLOG_INFO("Sending chat JSON: {}", json);
    mcp_client_->SendRequest(json);
  }
  
  void SendMCPRequest(const std::string& method, const std::string& params) {
    if (comm_mode_ != CommMode::IPC || !mcp_client_) return;
    
    std::stringstream json;
    json << "{\"jsonrpc\":\"2.0\",\"method\":\"" << method 
         << "\",\"id\":" << ++message_id_;
    if (!params.empty()) {
      json << ",\"params\":" << params;
    } else {
      json << ",\"params\":{}";
    }
    json << "}";
    
    SPDLOG_INFO("Sending MCP request: {}", json.str());
    mcp_client_->SendRequest(json.str());
  }
  
  void SendToolCall(const std::string& toolName, const std::string& args) {
    if (comm_mode_ != CommMode::IPC || !mcp_client_) return;
    
    std::stringstream json;
    json << "{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":" << ++message_id_ 
         << ",\"params\":{\"name\":\"" << EscapeJSON(toolName) << "\"";
    if (!args.empty()) {
      json << ",\"arguments\":" << args; // Assume args is already JSON
    }
    json << "}}";
    
    mcp_client_->SendRequest(json.str());
  }
  
  std::string EscapeJSON(const std::string& str) {
    std::string result;
    for (char c : str) {
      switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += c; break;
      }
    }
    return result;
  }

  void ProcessCommand(const std::string& command) {
    if (command.empty()) return;
    
    // Trim whitespace from the end
    std::string trimmed_command = command;
    trimmed_command.erase(trimmed_command.find_last_not_of(" \t\n\r\f\v") + 1);
    
    if (trimmed_command.empty()) return;
    
    SPDLOG_INFO("Processing command: '{}' (mode: {})", trimmed_command, 
                comm_mode_ == CommMode::IPC ? "IPC" : "STANDALONE");
    state_.AddToHistory(trimmed_command);
    
    // Add user input to log
    AddLogEntryWithNotification(LogEntryType::USER, trimmed_command);
    
    // Clear any retry state when new command is entered
    state_.ClearRetry();
    
    // Check if it's a slash command
    if (trimmed_command[0] == '/') {
      std::string cmd = trimmed_command.substr(1); // Remove the '/'

      if (cmd == "model" || cmd.rfind("model ", 0) == 0) {
        if (cmd == "model") {
          // Show available models and usage
          std::string model_info = 
            "🤖 Model Selection\n"
            "\n"
            "Current: " + config_.model + " (reasoning: " + config_.reasoning_effort + ")\n"
            "\n"
            "Available Models:\n"
            "  • kimi - Kimi K2 (fast, cost-effective)\n"
            "  • o3   - OpenAI o3 (reasoning model with thinking tokens)\n"
            "\n"
            "Usage:\n"
            "  /model kimi  - Switch to Kimi K2\n"
            "  /model o3    - Switch to OpenAI o3\n"
            "  /think <lvl> - Set reasoning effort (o3 only)\n"
            "\n"
            "Reasoning Levels:\n"
            "  minimal, low, medium, high";
          state_.AddLogEntry(LogEntryType::SYSTEM, model_info);
        } else {
          // Set new model
          std::string m = cmd.substr(6);
          if (m == "kimi" || m == "o3") {
            config_.model = m;
            SaveConfig();  // Persist the change
            state_.AddLogEntry(LogEntryType::SYSTEM,
                               "Model switched to «" + m + "»");
          } else {
            state_.AddLogEntry(LogEntryType::ERROR,
                               "Unknown model: " + m + "  (use kimi | o3)");
          }
        }
        return;
      }

      if (cmd == "think" || cmd.rfind("think ", 0) == 0) {
        if (cmd == "think") {
          // Show reasoning effort options
          std::string think_info = 
            "🧠 Reasoning Effort (OpenAI o3)\n"
            "\n"
            "Current: " + config_.reasoning_effort + " (model: " + config_.model + ")\n"
            "\n"
            "Available Levels:\n"
            "  • minimal - Fast, basic reasoning\n"
            "  • low     - Light reasoning effort\n"
            "  • medium  - Balanced reasoning (default)\n"
            "  • high    - Deep reasoning, more thinking tokens\n"
            "\n"
            "Usage:\n"
            "  /think minimal  - Set minimal effort\n"
            "  /think low      - Set low effort\n"
            "  /think medium   - Set medium effort\n"
            "  /think high     - Set high effort\n"
            "\n"
            "Note: Only affects o3 model. Use /model o3 first.";
          state_.AddLogEntry(LogEntryType::SYSTEM, think_info);
        } else {
          // Set new effort
          std::string e = cmd.substr(6);
          static const std::set<std::string> ok =
              {"minimal","low","medium","high"};
          if (ok.count(e)) {
            config_.reasoning_effort = e;
            SaveConfig();  // Persist the change
            state_.AddLogEntry(LogEntryType::SYSTEM,
                               "Reasoning effort set to «" + e + "»");
            if (config_.model != "o3") {
              state_.AddLogEntry(LogEntryType::SYSTEM,
                                 "Note: Reasoning effort only affects o3 model. Current: " + config_.model);
            }
          } else {
            state_.AddLogEntry(LogEntryType::ERROR,
                               "Bad effort: " + e +
                               "  (use minimal | low | medium | high)");
          }
        }
        return;
      }
      
      if (cmd == "help" || cmd == "h") {
        SPDLOG_DEBUG("Showing help");
        std::string help_text = 
          "Available commands:\n"
          "\n"
          "  /help    - Show this help\n"
          "  /model <kimi|o3>    → choose back-end model\n"
          "  /think <lvl>        → minimal | low | medium | high\n"
          "  /tools   - List available tools\n"
          "  /servers - Show connected servers\n"
          "  /clear   - Clear conversation history\n"
          "  /list    - List saved conversations\n"
          "  /load N  - Load conversation #N\n"
          "  /delete N - Delete conversation #N\n"
          "  /new     - Start new chat conversation\n"
          "  /sync    - Check chat history synchronization\n"
          "  /exit    - Exit the application (aliases: /quit, /q)\n"
          "\n"
          "Keyboard shortcuts:\n"
          "  Enter    - Insert newline in input\n"
          "  Ctrl+N   - Send message\n"
          "  Ctrl+C   - Press twice to exit\n"
          "\n"
          "Type any message without a slash to chat.";
        AddLogEntryWithNotification(LogEntryType::SYSTEM, help_text);
      } else if (cmd == "tools") {
        SPDLOG_DEBUG("Listing MCP tools");
        if (comm_mode_ == CommMode::IPC) {
          if (!mcp_client_ || !mcp_client_->IsConnected()) {
            SPDLOG_ERROR("MCP server not connected");
            AddLogEntryWithNotification(LogEntryType::ERROR, "Cannot list tools: MCP server not connected");
          } else {
            SendMCPRequest("tools/list", "{}");
            AddLogEntryWithNotification(LogEntryType::SYSTEM, "Requesting tool list...");
          }
        } else {
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "No tools available in standalone mode");
        }
      } else if (cmd == "servers") {
        SPDLOG_DEBUG("Showing connected servers");
        if (mcp_client_ && mcp_client_->IsConnected()) {
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "Connected to TCP server at 127.0.0.1:4000");
        } else {
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "No servers connected");
        }
      } else if (cmd.substr(0, 4) == "use ") {
        // Parse /use <tool> [args]
        std::string toolCmd = cmd.substr(4);
        size_t spacePos = toolCmd.find(' ');
        std::string toolName = (spacePos != std::string::npos) 
          ? toolCmd.substr(0, spacePos) 
          : toolCmd;
        std::string args = (spacePos != std::string::npos) 
          ? toolCmd.substr(spacePos + 1) 
          : "";
        
        SPDLOG_DEBUG("Executing MCP tool: {} with args: {}", toolName, args);
        if (comm_mode_ == CommMode::IPC) {
          SendToolCall(toolName, args);
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "Executing tool: " + toolName);
        } else {
          AddLogEntryWithNotification(LogEntryType::ERROR, "Tool execution not available in standalone mode");
        }
      } else if (cmd == "clear") {
        state_.ClearEventLog();
        AddLogEntryWithNotification(LogEntryType::SYSTEM, "Conversation cleared");
      } else if (cmd == "list") {
        // List available chat history files
        auto histories = state_.GetAvailableChatHistories();
        if (histories.empty()) {
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "No saved conversations found");
        } else {
          std::string list_output = "📚 Saved Conversations:\n\n";
          int index = 1;
          const std::string current_file = ".data/chat-history.json";
          
          for (const auto& [path, preview] : histories) {
            bool is_current = (path == current_file);
            auto ftime = std::filesystem::last_write_time(path);
            std::string time_str = state_.FormatTimestamp(ftime, is_current);
            
            list_output += std::to_string(index++) + ". " + time_str + "\n";
            list_output += "   \"" + preview + "\"\n\n";
          }
          
          list_output += "Commands: /load N  /delete N";
          AddLogEntryWithNotification(LogEntryType::SYSTEM, list_output);
        }
      } else if (cmd.starts_with("load ")) {
        // Load a specific chat history
        try {
          size_t index = std::stoul(cmd.substr(5));
          if (state_.LoadChatHistory(index)) {
            // Clear event log and reload
            state_.ClearEventLog();
            state_.LoadChatHistoryOnStartup();
            AddLogEntryWithNotification(LogEntryType::SYSTEM, 
                                        "Loaded conversation #" + std::to_string(index));
          } else {
            AddLogEntryWithNotification(LogEntryType::ERROR, 
                                        "Failed to load conversation #" + std::to_string(index));
          }
        } catch (const std::exception& e) {
          AddLogEntryWithNotification(LogEntryType::ERROR, "Invalid index for /load command");
        }
      } else if (cmd.starts_with("delete ")) {
        // Delete a specific chat history
        try {
          size_t index = std::stoul(cmd.substr(7));
          if (state_.DeleteChatHistory(index)) {
            AddLogEntryWithNotification(LogEntryType::SYSTEM, 
                                        "Deleted conversation #" + std::to_string(index));
          } else {
            AddLogEntryWithNotification(LogEntryType::ERROR, 
                                        "Failed to delete conversation #" + std::to_string(index) + 
                                        " (cannot delete current conversation)");
          }
        } catch (const std::exception& e) {
          AddLogEntryWithNotification(LogEntryType::ERROR, "Invalid index for /delete command");
        }
      } else if (cmd == "new") {
        // Rotate chat history file, reset token count, clear conversation log, and send /new command to server
        if (comm_mode_ == CommMode::IPC && mcp_client_ && mcp_client_->IsConnected()) {
          state_.RotateChatHistoryFile();
          state_.ResetTokenCount();
          state_.ClearEventLog();
          std::string request = R"({"type": "chat", "content": "/new"})";
          mcp_client_->SendRequest(request);
          SPDLOG_DEBUG("Rotated chat history file, reset token count and cleared conversation log");
        } else {
          AddLogEntryWithNotification(LogEntryType::ERROR, "New conversation command requires connection to MCP server");
        }
      } else if (cmd == "sync") {
        // Request chat history sync check from server
        RequestSyncCheck(true);  // Enable retry on first attempt
      } else if (cmd == "reset") {
        // Clear awaiting response state manually and notify server
        if (state_.IsAwaitingResponse()) {
          // Send reset command to server to clear its processing state
          if (comm_mode_ == CommMode::IPC && mcp_client_ && mcp_client_->IsConnected()) {
            std::string request = R"({"type": "reset"})";
            mcp_client_->SendRequest(request);
            AddLogEntryWithNotification(LogEntryType::SYSTEM, "Sent reset request to server");
          }
          
          state_.ClearAwaitingResponse();
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "Manually reset awaiting response state");
        } else {
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "No active request to reset");
        }
      } else if (cmd == "dump") {
        SPDLOG_DEBUG("Dump screen requested");
        DumpScreen();
      } else if (cmd == "exit" || cmd == "quit" || cmd == "q") {
        SPDLOG_INFO("Exit command received");
        state_.RequestExit();
      } else {
        SPDLOG_WARN("Unknown command: /{}", cmd);
        AddLogEntryWithNotification(LogEntryType::ERROR, "Unknown command: /" + cmd);
      }
    } else {
      // Non-slash commands go to chatbot
      SPDLOG_INFO("Chatbot message: {}", trimmed_command);
      
      if (comm_mode_ == CommMode::IPC) {
        SPDLOG_INFO("Sending chat message to MCP server");
        
        SendChatMessage(trimmed_command);
        
        // Only write to chat history and add system message if not blocked
        if (state_.IsAwaitingResponse()) {
          // Write user message to chat history now that we're successfully awaiting
          state_.WriteToChatHistory("user", trimmed_command);
          AddLogEntryWithNotification(LogEntryType::SYSTEM, "[Awaiting response...]");
        }
      } else {
        SPDLOG_INFO("In standalone mode - showing not implemented");
        AddLogEntryWithNotification(LogEntryType::RESPONSE, "Chat functionality requires connection to MCP server");
      }
    }
  }

  bool HandleEvent(const Event& event) {
    if (event == Event::CtrlC) {
      state_.HandleCtrlC();
      return true;
    }
    
    // Handle Escape key as interrupt when awaiting response
    if (event == Event::Escape && state_.IsAwaitingResponse()) {
      SendInterrupt();
      return true;
    }
    
    // Reset Ctrl+C and Esc on other input
    if (event.is_character()) {
      state_.ResetCtrlC();
      state_.ResetEsc();
    }
    // Ctrl+N handling should be done in the Application's input component wrapper
    // Not here in InputHandler
    
    return false;
  }
};

// ============================================================================
// UI Renderer - Pure rendering logic, no state mutations
// ============================================================================
class UIRenderer {
private:
  const Config& config_;
  const StateManager& state_;
  const std::vector<Tool>& tools_;
  bool is_connected_ = false;

  Elements RenderHelp() const {
    return {
      text("▶ CLI COMMANDS") | bold | color(Colors::kBrightGreen),
      text(""),
      hbox(text("  /help      ") | color(Colors::kCyan), 
           text("→ Show this help message") | color(Colors::kDimGreen)),
      hbox(text("  /tools     ") | color(Colors::kCyan), 
           text("→ List available MCP tools") | color(Colors::kDimGreen)),
      hbox(text("  /servers   ") | color(Colors::kCyan), 
           text("→ Show connected MCP servers") | color(Colors::kDimGreen)),
      hbox(text("  /connect   ") | color(Colors::kCyan), 
           text("→ Connect to an MCP server") | color(Colors::kDimGreen)),
      hbox(text("  /new       ") | color(Colors::kCyan), 
           text("→ Start new chat conversation") | color(Colors::kDimGreen)),
      hbox(text("  /sync      ") | color(Colors::kCyan), 
           text("→ Check chat history synchronization") | color(Colors::kDimGreen)),
      hbox(text("  /dump      ") | color(Colors::kCyan), 
           text("→ Save current screen to file") | color(Colors::kDimGreen)),
      hbox(text("  /exit      ") | color(Colors::kCyan), 
           text("→ Exit the application (alias: /q)") | color(Colors::kDimGreen)),
      text(""),
      separator() | color(Colors::kPurple),
      text(""),
      text("▶ MCP TOOL USAGE") | bold | color(Colors::kBrightGreen),
      text(""),
      hbox(text("  /use <tool> [args]  ") | color(Colors::kCyan),
           text("→ Execute an MCP tool") | color(Colors::kDimGreen)),
      text(""),
      separator() | color(Colors::kPurple),
      text(""),
      text("▶ CHAT MODE") | bold | color(Colors::kBrightGreen),
      text(""),
      text("Any text without a slash is sent to the AI assistant") | color(Colors::kGray)
    };
  }

  Elements RenderToolList() const {
    Elements elements;
    
    elements.push_back(text("◉ AVAILABLE TOOLS") | bold | color(Colors::kBrightGreen));
    elements.push_back(text(""));
    
    std::map<std::string, std::vector<const Tool*>> toolsByCategory;
    for (const auto& tool : tools_) {
      toolsByCategory[tool.category].push_back(&tool);
    }
    
    for (const auto& [category, categoryTools] : toolsByCategory) {
      elements.push_back(text("▸ " + category) | bold | color(Colors::kPink));
      for (const auto* tool : categoryTools) {
        elements.push_back(hbox(
          text("    ◆ ") | color(Colors::kPurple),
          text(tool->name) | bold | color(Colors::kCyan)
        ));
        elements.push_back(text("      " + tool->description) | color(Colors::kGray));
      }
      elements.push_back(text(""));
    }
    
    return elements;
  }

  Elements RenderToolInfo(const Tool& tool) const {
    Elements elements;
    
    elements.push_back(text("◆ " + tool.name) | bold | color(Colors::kBrightGreen));
    elements.push_back(text("  " + tool.category) | color(Colors::kPurple));
    elements.push_back(text("  " + tool.description) | color(Colors::kDimGreen));
    elements.push_back(separator() | color(Colors::kPurple));
    
    if (!tool.parameters.empty()) {
      elements.push_back(text("▸ Parameters") | bold | color(Colors::kCyan));
      for (const auto& param : tool.parameters) {
        std::string paramStr = "  • " + param.name + " (" + param.type + ")";
        if (param.required) {
          elements.push_back(text(paramStr + " [required]") | color(Colors::kHotPink));
        } else {
          elements.push_back(text(paramStr + " [optional]") | color(Colors::kGreen));
        }
        elements.push_back(text("    " + param.description) | color(Colors::kGray));
      }
    }
    
    return elements;
  }

public:
  UIRenderer(const Config& config, const StateManager& state, const std::vector<Tool>& tools)
    : config_(config), state_(state), tools_(tools) {}
    
  void SetConnectionStatus(bool connected) {
    is_connected_ = connected;
  }

  std::string GetScreenText() const {
    std::stringstream ss;
    
    // Header
    ss << config_.welcomeMessage << "\n";
    ss << config_.serverName << " v" << config_.serverVersion;
    ss << "  [Connection: " << (is_connected_ ? "✓" : "✗") << "]\n";
    ss << "================================================================================\n\n";
    
    // Main content based on state
    switch (state_.GetDisplayMode()) {
      case StateManager::DisplayMode::HELP:
        ss << "▶ CLI COMMANDS\n\n";
        ss << "  /help      → Show this help message\n";
        ss << "  /tools     → List available MCP tools\n";
        ss << "  /servers   → Show connected MCP servers\n";
        ss << "  /connect   → Connect to an MCP server\n";
        ss << "  /new       → Start new chat conversation\n";
        ss << "  /sync      → Check chat history synchronization\n";
        ss << "  /dump      → Save current screen to file\n";
        ss << "  /exit      → Exit the application (alias: /q)\n\n";
        ss << "▶ MCP TOOL USAGE\n\n";
        ss << "  /use <tool> [args]  → Execute an MCP tool\n\n";
        ss << "▶ CHAT MODE\n\n";
        ss << "Any text without a slash is sent to the AI assistant\n";
        break;
        
      case StateManager::DisplayMode::LIST:
        ss << "◉ AVAILABLE TOOLS\n\n";
        {
          std::map<std::string, std::vector<const Tool*>> toolsByCategory;
          for (const auto& tool : tools_) {
            toolsByCategory[tool.category].push_back(&tool);
          }
          
          for (const auto& [category, categoryTools] : toolsByCategory) {
            ss << "▸ " << category << "\n";
            for (const auto* tool : categoryTools) {
              ss << "    ◆ " << tool->name << "\n";
              ss << "      " << tool->description << "\n";
            }
            ss << "\n";
          }
        }
        break;
        
      case StateManager::DisplayMode::TOOL_INFO:
        if (auto* tool = state_.GetSelectedTool()) {
          ss << "◆ " << tool->name << "\n";
          ss << "  " << tool->category << "\n";
          ss << "  " << tool->description << "\n";
          ss << "--------------------------------------------------------------------------------\n";
          
          if (!tool->parameters.empty()) {
            ss << "▸ Parameters\n";
            for (const auto& param : tool->parameters) {
              ss << "  • " << param.name << " (" << param.type << ")";
              if (param.required) {
                ss << " [required]\n";
              } else {
                ss << " [optional]\n";
              }
              ss << "    " << param.description << "\n";
            }
          }
        }
        break;
        
      case StateManager::DisplayMode::UNKNOWN_COMMAND:
        ss << "⚠ Unknown command. Commands must start with '/'\n";
        ss << "Type /help for available commands\n";
        break;
        
      case StateManager::DisplayMode::EXIT_WARNING:
        ss << "⚠ Press Ctrl+C again to exit\n";
        break;
        
      case StateManager::DisplayMode::EXIT_MESSAGE:
        ss << "◆ SEE YOU IN THE CYBER WORLD ◆\n";
        break;
        
      case StateManager::DisplayMode::DUMP_SUCCESS:
        ss << "✓ Screen dumped to file\n";
        break;
        
      case StateManager::DisplayMode::CHAT_NOT_IMPLEMENTED:
        ss << "🤖 CHAT MODE\n\n";
        ss << "The AI assistant feature is not implemented yet.\n";
        ss << "For now, use slash commands to interact with tools.\n\n";
        ss << "Type /help to see available commands.\n";
        break;
        
      case StateManager::DisplayMode::MCP_ERROR:
        ss << "⚠️ MCP CONNECTION ERROR\n\n";
        ss << "Cannot connect to MCP server\n\n";
        ss << "Possible causes:\n";
        ss << "• MCP server failed to start\n";
        ss << "• Socket connection timeout\n";
        ss << "• Server crashed or was terminated\n\n";
        ss << "Try running in standalone mode:\n";
        ss << "  ./src/demo\n";
        break;
        
      case StateManager::DisplayMode::NORMAL:
        // Normal mode doesn't need special display in GetScreenText
        break;
    }
    
    // Footer info
    ss << "\n================================================================================\n";
    if (state_.IsCtrlCPending()) {
      ss << "◉ Press Ctrl+C again to exit\n";
    } else if (!state_.GetHistory().empty()) {
      ss << "◉ Last: " << state_.GetHistory().back() << "\n";
    } else {
      ss << "◉ Type /help for commands • Ctrl+C twice to exit\n";
    }
    
    return ss.str();
  }

  Element RenderConversationLog() const {
    Elements log_lines;
    const auto& log = state_.GetEventLog();
    
    for (const auto& entry : log) {
      Element line;
      
      // Special handling for empty content - just show a blank line
      if (entry.content.empty()) {
        line = text("");
      } else {
        // Format timestamp
        auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
        char time_str[20];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));
        
        switch (entry.type) {
          case LogEntryType::USER:
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("You: ") | bold | color(Colors::kCyan),
              paragraph(entry.content) | color(Colors::kGreen)
            });
            break;
          case LogEntryType::SYSTEM:
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("System: ") | bold | color(Colors::kPurple),
              paragraph(entry.content) | color(Colors::kDimGreen)
            });
            break;
          case LogEntryType::RESPONSE:
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("Assistant: ") | bold | color(Colors::kPink),
              paragraph(entry.content) | color(Colors::kGray)
            });
            break;
          case LogEntryType::ERROR:
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("Error: ") | bold | color(Colors::kHotPink),
              paragraph(entry.content) | color(Colors::kHotPink)
            });
            break;
          case LogEntryType::BLOCKED:
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("Blocked: ") | bold | color(Colors::kPurple),
              paragraph(entry.content) | color(Colors::kPurple)
            });
            break;
        }
      }
      
      log_lines.push_back(line);
    }
    
    if (log_lines.empty()) {
      log_lines.push_back(
        text("Welcome! Type a message or use /help for available commands.") 
        | color(Colors::kGray) | center
      );
    }
    
    return vbox(std::move(log_lines));
  }
  
  Element Render() const {
    // Exit immediately if we're exiting
    if (state_.IsExiting()) {
      return vbox({
        text("◆ SEE YOU IN THE CYBER WORLD ◆") | bold | color(Colors::kBrightGreen) | center
      }) | bgcolor(Colors::kBackground);
    }

    // Connection status
    auto connection_status = hbox({
      text(is_connected_ ? "●" : "○") | color(is_connected_ ? Colors::kGreen : Colors::kHotPink),
      text(" ") | color(Colors::kGray),
      text(is_connected_ ? "Connected" : "Disconnected") | color(Colors::kGray)
    });
    
    // Header - fixed height
    auto header = hbox({
      text(" ") | color(Colors::kPurple),
      text(config_.welcomeMessage) | bold | color(Colors::kBrightGreen),
      filler(),
      connection_status,
      text(" ") | color(Colors::kPurple)
    }) | border | color(Colors::kPurple);

    // Middle content - scrollable conversation log
    auto content = RenderConversationLog();
    
    return vbox({
      header | size(HEIGHT, EQUAL, 3),
      content | flex | frame | focusPositionRelative(0, 1) | vscroll_indicator,
    }) | bgcolor(Colors::kBackground);
  }
};

// Implementation of InputHandler::DumpScreen() - must be after UIRenderer definition
void InputHandler::DumpScreen() {
  if (!renderer_) {
    SPDLOG_ERROR("No renderer set for screen dump");
    return;
  }
  
  // Generate timestamp for filename using fmt
  auto now = std::chrono::system_clock::now();
  std::string filename = fmt::format("dump_{:%Y%m%d_%H%M%S}.txt", now);
  
  try {
    std::ofstream file(filename);
    if (file.is_open()) {
      file << renderer_->GetScreenText();
      file.close();
      SPDLOG_INFO("Screen dumped to {}", filename);
      state_.SetDisplayMode(StateManager::DisplayMode::DUMP_SUCCESS);
    } else {
      SPDLOG_ERROR("Failed to open file for writing: {}", filename);
    }
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Error dumping screen: {}", e.what());
  }
}

// ============================================================================
// Application - Main controller that coordinates everything
// ============================================================================
class Application {
private:
  Config config_;
  StateManager state_;
  std::unique_ptr<UIRenderer> renderer_;
  std::unique_ptr<InputHandler> input_handler_;
  std::unique_ptr<MCPClient> mcp_client_;
  int initial_focus = 2;
  
  ScreenInteractive screen_;
  Closure exit_closure_;
  
  std::string user_input_;
  Component input_with_history_;
  Component send_button_;
  Component retry_button_;
  Component event_log_;
  ConversationLogManager log_manager_;
  CommMode comm_mode_ = CommMode::STANDALONE;
  
  // Timeout monitoring
  StateManager::TimeoutState last_timeout_state_ = StateManager::TimeoutState::NORMAL;

public:
  Application() : screen_(ScreenInteractive::Fullscreen()) {
    // Clear screen on startup
    std::cout << "\033[2J\033[H" << std::flush;
    
    // Load configuration
    LoadConfig("./config.json");
    
    // Load existing chat history and restore token count
    state_.LoadChatHistoryOnStartup();
    
    // Initialize components
    state_.SetMaxHistorySize(config_.maxHistorySize);
    renderer_ = std::make_unique<UIRenderer>(config_, state_, config_.tools);
    input_handler_ = std::make_unique<InputHandler>(state_, config_.tools);
    input_handler_->SetRenderer(renderer_.get());
    input_handler_->SetLogManager(&log_manager_);
    
    // Set screen reference for immediate UI updates
    log_manager_.SetScreen(&screen_);
    
    // Create conversation log component using Renderer
    auto event_log_renderer = Renderer([this] {
      Elements log_elements;
      const auto& log = state_.GetEventLog();
      
      // Render all log entries
      for (const auto& entry : log) {
        Element line;
        
        // Format timestamp
        auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
        char time_str[20];
        std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));
        
        // Get terminal width and calculate content width
        int terminal_width = Terminal::Size().dimx;
        
        switch (entry.type) {
          case LogEntryType::USER: {
            // "[HH:MM:SS] You: " = 1 + 8 + 2 + 5 = 16 chars + 3 scroll bar + 5 gutter = 24
            int prefix_width = 24;
            int content_width = terminal_width - prefix_width;
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("You: ") | bold | color(Colors::kCyan),
              paragraph(entry.content) | color(Colors::kGreen) | size(WIDTH, LESS_THAN, content_width)
            });
            break;
          }
          case LogEntryType::SYSTEM: {
            // "[HH:MM:SS] System: " = 1 + 8 + 2 + 8 = 19 chars + 3 scroll bar + 5 gutter = 27
            int prefix_width = 27;
            int content_width = terminal_width - prefix_width;
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("System: ") | bold | color(Colors::kPurple),
              paragraph(entry.content) | color(Colors::kDimGreen) | size(WIDTH, LESS_THAN, content_width)
            });
            break;
          }
          case LogEntryType::RESPONSE: {
            // "[HH:MM:SS] Assistant: " = 1 + 8 + 2 + 11 = 22 chars + 3 scroll bar + 5 gutter = 30
            int prefix_width = 30;
            int content_width = terminal_width - prefix_width;
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("Assistant: ") | bold | color(Colors::kPink),
              paragraph(entry.content) | color(Colors::kGray) | size(WIDTH, LESS_THAN, content_width)
            });
            break;
          }
          case LogEntryType::ERROR: {
            // "[HH:MM:SS] Error: " = 1 + 8 + 2 + 7 = 18 chars + 3 scroll bar + 5 gutter = 26
            int prefix_width = 26;
            int content_width = terminal_width - prefix_width;
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("Error: ") | bold | color(Colors::kHotPink),
              paragraph(entry.content) | color(Colors::kHotPink) | size(WIDTH, LESS_THAN, content_width)
            });
            break;
          }
          case LogEntryType::BLOCKED: {
            // "[HH:MM:SS] Blocked: " = 1 + 8 + 2 + 9 = 20 chars + 3 scroll bar + 5 gutter = 28
            int prefix_width = 28;
            int content_width = terminal_width - prefix_width;
            line = hbox({
              text("[") | color(Colors::kGray),
              text(time_str) | color(Colors::kGray),
              text("] ") | color(Colors::kGray),
              text("Blocked: ") | bold | color(Colors::kPurple),
              paragraph(entry.content) | color(Colors::kPurple) | size(WIDTH, LESS_THAN, content_width)
            });
            break;
          }
        }
        
        log_elements.push_back(line);
      }
      
      // If empty, show placeholder
      if (log_elements.empty()) {
        log_elements.push_back(
          text("Welcome! Type a message or use /help for available commands.") 
          | color(Colors::kGray) | center
        );
      }
      
      // Create the scrollable log with proper relative positioning
      return vbox(std::move(log_elements))
           | focusPositionRelative(0.0f, log_manager_.scroll_y_)  // Use relative positioning
           | frame
           | flex
           | vscroll_indicator;
    });
    
    // Wrap with event handler for scrolling
    event_log_ = CatchEvent(event_log_renderer, [this](Event event) {
      return log_manager_.HandleScrollEvent(event);
    });
    
    // Setup screen
    screen_.ForceHandleCtrlC(false);
    exit_closure_ = screen_.ExitLoopClosure();
    
    // Create multiline input component with history
    InputOption input_options;
    input_options.placeholder = config_.inputPlaceholder;
    input_options.multiline = true;  // Enable multiline mode
    // NO on_enter callback - let Enter insert newlines naturally
    
    // Custom transform to keep colors unchanged when focused
    input_options.transform = [](InputState state) {
      state.element |= color(Colors::kGreen);  // Keep text green
      
      if (state.is_placeholder) {
        state.element |= dim;
      }
      
      // Don't change colors when focused - just show cursor
      // The cursor will be automatically displayed by FTXUI
      
      return state.element;
    };
    
    // Create the input with history component
    auto input_with_history_impl = std::make_shared<InputWithHistory>(&user_input_, input_options);
    
    // Wrap to handle Ctrl+N as send command and double-Esc to clear
    input_with_history_ = CatchEvent(input_with_history_impl, [this, input_with_history_impl](Event event) {
      if (event == Event::CtrlN) {
        // Ctrl+N sends the message
        if (!user_input_.empty()) {
          SPDLOG_DEBUG("Ctrl+N pressed - sending message: {}", user_input_);
          std::string command = user_input_;
          
          // Add to command history
          input_with_history_impl->AddToHistory(command);
          
          user_input_.clear();
          input_handler_->ProcessCommand(command);
        }
        return true;  // Consume Ctrl+N
      }
      
      if (event == Event::Escape) {
        // Handle the Esc and check if we should clear
        bool should_clear = state_.HandleEsc();
        if (should_clear) {
          SPDLOG_DEBUG("Double Esc - clearing input");
          user_input_.clear();
        }
        return true;  // Consume the Escape event to prevent default behavior
      }
      
      return false;  // Let other events pass through (including Enter for newlines and arrow keys)
    });
    
    // Create a clickable send button
    send_button_ = Button("Send", [this, input_with_history_impl] {
      if (!user_input_.empty()) {
        SPDLOG_DEBUG("Send button clicked: {}", user_input_);
        std::string command = user_input_;
        
        // Add to command history
        input_with_history_impl->AddToHistory(command);
        
        user_input_.clear();
        input_handler_->ProcessCommand(command);
      }
    });
    
    // Create retry button (🔄 is a red retry icon-like symbol)
    retry_button_ = Button("🔄 Retry", [this] {
      SPDLOG_DEBUG("Retry button clicked");
      input_handler_->SendRetryRequest();
    });
  }

  void CheckTimeoutState() {
    auto current_state = state_.GetTimeoutState();
    
    // Only act on state transitions to avoid spam
    if (current_state != last_timeout_state_) {
      switch (current_state) {
        case StateManager::TimeoutState::SERVER_OVERDUE: {
          auto seconds = state_.GetElapsedTime().count();
          input_handler_->AddLogEntryWithNotification(LogEntryType::SYSTEM, 
            "Server overdue (" + std::to_string(seconds / 60) + ":" + 
            std::to_string(seconds % 60) + ") - press <Esc> or type /reset to cancel");
          break;
        }
        case StateManager::TimeoutState::RECOMMEND_RESET: {
          auto seconds = state_.GetElapsedTime().count();
          input_handler_->AddLogEntryWithNotification(LogEntryType::SYSTEM, 
            "Server not responding (" + std::to_string(seconds / 60) + ":" + 
            std::to_string(seconds % 60) + ") - press <Esc> or /reset to interrupt");
          break;
        }
        case StateManager::TimeoutState::AUTO_RESET: {
          auto seconds = state_.GetElapsedTime().count();
          input_handler_->AddLogEntryWithNotification(LogEntryType::SYSTEM, 
            "Auto-reset: Server failed to respond after " + std::to_string(seconds / 60) + ":" + 
            std::to_string(seconds % 60));
          state_.ClearAwaitingResponse();
          break;
        }
        case StateManager::TimeoutState::NORMAL:
          // No action needed for normal state
          break;
      }
      last_timeout_state_ = current_state;
    }
  }

  void SetCommMode(CommMode mode, const std::string& host = "127.0.0.1", int port = 4000) {
    comm_mode_ = mode;
    input_handler_->SetCommMode(mode);
    
    // Connect to TCP server if in IPC mode
    if (mode == CommMode::IPC) {
      SPDLOG_INFO("Attempting to connect to TCP server at: {}:{}", host, port);
      
      mcp_client_ = std::make_unique<MCPClient>(host, port);
      if (mcp_client_->Connect()) {
        input_handler_->SetMCPClient(mcp_client_.get());
        
        // Set up callback to handle server responses
        mcp_client_->SetResponseCallback([this](const std::string& type, const std::string& content) {
          if (type == "RESPONSE") {
            // Clear awaiting response state
            state_.ClearAwaitingResponse();
            
            // Write assistant response to chat history
            state_.WriteToChatHistory("assistant", content);
            input_handler_->AddLogEntryWithNotification(LogEntryType::RESPONSE, content);
          } else if (type == "SYSTEM") {
            input_handler_->AddLogEntryWithNotification(LogEntryType::SYSTEM, content);
          } else if (type == "SYNC") {
            // Process sync response
            try {
              dom::parser parser;
              dom::element doc;
              auto error = parser.parse(content).get(doc);
              
              if (!error && doc["server_stats"].is_string() && doc["server_history"].is_array()) {
                std::string server_stats = std::string(doc["server_stats"].get_string().value());
                auto server_history = doc["server_history"].get_array();
                
                // Get file stats from StateManager
                std::string file_stats = state_.GetChatHistoryStats();
                
                // Perform detailed comparison
                std::string detailed_comparison = state_.CompareChatHistoryWithServer(server_history.value());
                
                std::string sync_result = file_stats + "\n" + 
                                         server_stats + "\n\n" +
                                         detailed_comparison;
                
                // Check if sync failed and retry is enabled
                bool sync_failed = detailed_comparison.find("✓ Perfect sync") == std::string::npos;
                
                if (sync_failed && input_handler_->sync_retry_enabled_) {
                  // First sync failed, try reload and retry
                  input_handler_->AddLogEntryWithNotification(LogEntryType::SYSTEM, sync_result);
                  input_handler_->RequestSyncCheck(false);  // Retry without further retries
                } else if (sync_failed && !input_handler_->sync_retry_enabled_) {
                  // Second sync failed, report critical error
                  sync_result += "\n\n❌ CRITICAL: Sync failed after reload - data integrity issue detected";
                  input_handler_->AddLogEntryWithNotification(LogEntryType::ERROR, sync_result);
                } else {
                  // Sync succeeded
                  input_handler_->AddLogEntryWithNotification(LogEntryType::SYSTEM, sync_result);
                }
              }
            } catch (const std::exception& e) {
              input_handler_->AddLogEntryWithNotification(LogEntryType::ERROR, "Sync check failed: " + std::string(e.what()));
            }
          } else if (type == "TIMEOUT_WITH_RETRY") {
            // Clear awaiting response state
            state_.ClearAwaitingResponse();
            
            // Parse timeout message and original message
            size_t delimiter_pos = content.find("|");
            if (delimiter_pos != std::string::npos) {
              std::string timeout_msg = content.substr(0, delimiter_pos);
              std::string original_msg = content.substr(delimiter_pos + 1);
              
              // Set retry state
              state_.SetRetryAvailable(timeout_msg, original_msg);
              
              // Add timeout message to log
              input_handler_->AddLogEntryWithNotification(LogEntryType::ERROR, timeout_msg);
            }
          } else if (type == "ERROR") {
            // Clear awaiting response state
            state_.ClearAwaitingResponse();
            
            input_handler_->AddLogEntryWithNotification(LogEntryType::ERROR, content);
          }
        });
        
        SPDLOG_INFO("TCP client connected successfully");
      }
    }
  }

  void Run() {
    // The conversation log component now handles scrolling internally
    
    // Create header
    auto header = Renderer([this] {
      auto context_size = hbox({
        text("Context Size: ") | color(Colors::kGray),
        text(std::to_string(state_.GetTotalTokens()) + " tokens") | color(Colors::kCyan)
      });
      
      auto connection_status = hbox({
        text(mcp_client_ && mcp_client_->IsConnected() ? "●" : "○") 
          | color(mcp_client_ && mcp_client_->IsConnected() ? Colors::kGreen : Colors::kHotPink),
        text(" ") | color(Colors::kGray),
        text(mcp_client_ && mcp_client_->IsConnected() ? "Connected" : "Disconnected") 
          | color(Colors::kGray)
      });
      
      return hbox({
        text(" ") | color(Colors::kPurple),
        text(config_.welcomeMessage) | bold | color(Colors::kBrightGreen),
        filler(),
        context_size,
        text("  ") | color(Colors::kGray),
        connection_status,
        text(" ") | color(Colors::kPurple)
      }) | border | color(Colors::kPurple);
    });
    
    // Create input container - conditionally include retry button
    auto input_container = Container::Horizontal({
      input_with_history_,
      send_button_,
      retry_button_
    });
    
    // Create vertical layout
    auto layout = Container::Vertical({
      header,
      event_log_,
      input_container
    }, &initial_focus);
    
    // Final renderer that composes everything
    auto main_component = Renderer(layout, [this, &header] {
      // Check timeout state and handle escalation
      CheckTimeoutState();
      
      // Check exit condition
      if (state_.IsExitRequested()) {
        state_.ConfirmExit();
        screen_.Post([this] { exit_closure_(); });
      }
      
      // Create status line
      bool has_input_text = !user_input_.empty();
      std::string status_msg = state_.GetStatusMessage(has_input_text);
      auto status_line = text(" " + status_msg) | color(Colors::kGray) | dim;
      
      return vbox({
        header->Render() | size(HEIGHT, EQUAL, 3),
        event_log_->Render() | flex,
        text(""),  // Small gap
        hbox({
          text(" ▶ ") | color(Colors::kHotPink),
          input_with_history_->Render() | color(Colors::kGreen) | flex,
          text(" ") | color(Colors::kPurple),
          send_button_->Render(),
          state_.IsRetryAvailable() ? text(" ") | color(Colors::kPurple) : text(""),
          state_.IsRetryAvailable() ? retry_button_->Render() | color(Colors::kHotPink) : text(""),
          text(" ") | color(Colors::kPurple)
        }) | border | color(Colors::kPurple) | bgcolor(Colors::kBackground),
        status_line,
        text("")  // Bottom padding
      }) | bgcolor(Colors::kBackground);
    });

    // Handle events
    auto event_handler = CatchEvent(main_component, [this](Event event) {
      // Let input handler process first
      if (input_handler_->HandleEvent(event)) {
        return true;
      }
      
      // The Enter key handling is now done in the InputOption on_enter callback
      // This allows the multiline input to properly handle Shift+Enter for newlines
      
      return false;
    });

    // Run the main loop
    screen_.Loop(event_handler);
  }

private:
  void LoadConfig(const std::string& filepath) {
    SPDLOG_INFO("Loading config from: {}", filepath);
    std::ifstream file(filepath);
    if (!file.is_open()) {
      SPDLOG_WARN("Config file not found at {}, using defaults", filepath);
      return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();
    
    dom::parser parser;
    dom::element doc;
    
    auto error = parser.parse(json_str).get(doc);
    if (error) {
      SPDLOG_ERROR("Failed to parse config file: {}", error_message(error));
      return;
    }
    
    SPDLOG_DEBUG("Config file parsed successfully");
    
    // Parse config (simplified for brevity)
    try {
      if (doc["ui"]["welcomeMessage"].is_string()) {
        config_.welcomeMessage = std::string(doc["ui"]["welcomeMessage"].get_string().value());
      }
      if (doc["ui"]["inputPlaceholder"].is_string()) {
        config_.inputPlaceholder = std::string(doc["ui"]["inputPlaceholder"].get_string().value());
      }
      if (doc["server"]["name"].is_string()) {
        config_.serverName = std::string(doc["server"]["name"].get_string().value());
      }
      if (doc["server"]["version"].is_string()) {
        config_.serverVersion = std::string(doc["server"]["version"].get_string().value());
      }
      if (doc["settings"]["maxHistorySize"].is_int64()) {
        config_.maxHistorySize = int(doc["settings"]["maxHistorySize"].get_int64().value());
      }
      
      // Parse tools
      if (doc["tools"].is_array()) {
        for (auto tool : doc["tools"].get_array().value()) {
          Tool t;
          if (tool["name"].is_string()) {
            t.name = std::string(tool["name"].get_string().value());
          }
          if (tool["description"].is_string()) {
            t.description = std::string(tool["description"].get_string().value());
          }
          if (tool["category"].is_string()) {
            t.category = std::string(tool["category"].get_string().value());
          }
          
          // Parse parameters
          if (tool["parameters"].is_object()) {
            for (auto [key, value] : tool["parameters"].get_object().value()) {
              ToolParameter param;
              param.name = std::string(key);
              
              if (value["type"].is_string()) {
                param.type = std::string(value["type"].get_string().value());
              }
              if (value["description"].is_string()) {
                param.description = std::string(value["description"].get_string().value());
              }
              if (value["required"].is_bool()) {
                param.required = value["required"].get_bool().value();
              } else {
                param.required = false;
              }
              
              t.parameters.push_back(param);
            }
          }
          
          // Parse examples
          if (tool["examples"].is_array()) {
            for (auto example : tool["examples"].get_array().value()) {
              if (example.is_string()) {
                t.examples.push_back(std::string(example.get_string().value()));
              }
            }
          }
          
          config_.tools.push_back(t);
        }
        SPDLOG_INFO("Loaded {} tools from config", config_.tools.size());
      }
    } catch (const std::exception& e) {
      SPDLOG_ERROR("Error reading config values: {}", e.what());
    }
  }
};

// ============================================================================
// Logging Setup
// ============================================================================
void SetupLogging() {
  try {
    // Create file sink only - no console output
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("cli.log", true);
    file_sink->set_level(spdlog::level::trace); // Everything to file
    
    // Create logger with file sink only
    auto logger = std::make_shared<spdlog::logger>("mcp", file_sink);
    
    // Set pattern with file location info
    logger->set_pattern("%Y-%m-%d %H:%M:%S.%e [%^%l%$] [%s:%#] %! › %v");
    
    // Register as default logger
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::trace);
    
    SPDLOG_INFO("=== MCP Server Starting ===");
  } catch (const std::exception& e) {
    std::cerr << "Failed to setup logging: " << e.what() << "\n";
  }
}

// ============================================================================
// Main
// ============================================================================
int main(int argc, char* argv[]) {
  SetupLogging();
  
  // Check for mode and TCP connection info
  CommMode mode = CommMode::IPC;  // Default to IPC mode with TCP
  std::string host = "127.0.0.1";
  int port = 4000;
  
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--standalone") {
      mode = CommMode::STANDALONE;
      SPDLOG_INFO("Running in standalone mode");
    } else if (std::string(argv[i]) == "--host" && i + 1 < argc) {
      host = argv[i + 1];
      i++;
    } else if (std::string(argv[i]) == "--port" && i + 1 < argc) {
      port = std::stoi(argv[i + 1]);
      i++;
    }
  }
  
  // Also check environment variables
  const char* env_host = getenv("MCP_HOST");
  if (env_host) {
    host = env_host;
  }
  const char* env_port = getenv("MCP_PORT");
  if (env_port) {
    port = std::stoi(env_port);
  }
  
  try {
    SPDLOG_INFO("Creating application instance");
    Application app;
    
    // Set communication mode
    app.SetCommMode(mode, host, port);
    
    SPDLOG_INFO("Starting application main loop");
    app.Run();
    
    SPDLOG_INFO("Application exited normally");
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Fatal error: {}", e.what());
    return 1;
  }
  
  spdlog::shutdown();
  return 0;
}
