#include "input_handler.hpp"

InputHandler::InputHandler(StateManager& state, std::vector<Tool>& tools) 
  : state_(state), tools_(tools) {
  LoadConfig();  // Load configuration on startup
}

void InputHandler::SetRenderer(UIRenderer* renderer) {
  renderer_ = renderer;
}

void InputHandler::SetLogManager(ConversationLogManager* manager) {
  log_manager_ = manager;
}

void InputHandler::SetCommMode(CommMode mode) {
  comm_mode_ = mode;
}

void InputHandler::SetMCPClient(MCPClient* client) {
  mcp_client_ = client;
}

Tool* InputHandler::FindTool(const std::string& name) {
  for (auto& tool : tools_) {
    if (tool.name == name) {
      return &tool;
    }
  }
  return nullptr;
}

void InputHandler::DumpScreen() {
  // Screen dump only available in UI mode
  SPDLOG_WARN("Screen dump not available in headless mode");
  return;
  
  // UI implementation would go here when UIRenderer is available
  // This is a stub for headless compilation
}

void InputHandler::RequestSyncCheck(bool retry) {
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

void InputHandler::AddLogEntryWithNotification(LogEntryType type, const std::string& content) {
  state_.AddLogEntry(type, content);
  // Notification to log manager only available in UI mode
  // In headless mode, we just update the state
}

void InputHandler::LoadConfig() {
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

void InputHandler::SaveConfig() {
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

void InputHandler::SendInterrupt() {
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

void InputHandler::SendRetryRequest() {
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

void InputHandler::SendChatMessage(const std::string& message) {
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

void InputHandler::SendMCPRequest(const std::string& method, const std::string& params) {
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

void InputHandler::SendToolCall(const std::string& toolName, const std::string& args) {
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

std::string InputHandler::EscapeJSON(const std::string& str) {
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

void InputHandler::ProcessCommand(const std::string& command) {
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
          
          // Notify server to reload the new chat history
          if (mcp_client_ && mcp_client_->IsConnected()) {
            std::string reload_request = R"({"type": "reload", "content": "chat_history"})";
            mcp_client_->SendRequest(reload_request);
            SPDLOG_INFO("Sent reload request to server after loading conversation #{}", index);
          }
          
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
      
      // Write user message to chat history BEFORE setting awaiting flag
      state_.WriteToChatHistory("user", trimmed_command);
      
      // Now send the message (which sets awaiting response flag)
      SendChatMessage(trimmed_command);
      
      // Add system message to indicate we're waiting
      if (state_.IsAwaitingResponse()) {
        AddLogEntryWithNotification(LogEntryType::SYSTEM, "[Awaiting response...]");
      }
    } else {
      SPDLOG_INFO("In standalone mode - showing not implemented");
      AddLogEntryWithNotification(LogEntryType::RESPONSE, "Chat functionality requires connection to MCP server");
    }
  }
}

bool InputHandler::HandleEvent(const app::Event& event) {
  if (event == app::EventType::CtrlC) {
    state_.HandleCtrlC();
    return true;
  }
  
  // Handle Escape key as interrupt when awaiting response
  if (event == app::EventType::Escape && state_.IsAwaitingResponse()) {
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