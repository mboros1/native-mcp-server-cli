#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/chrono.h>

#include "concurrent_queue.hpp"
#include "event.hpp"
#include "tcp_client.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/event.hpp>
#include <simdjson.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <chrono>
#include <vector>
#include <map>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>

using namespace ftxui;
using namespace simdjson;

// Forward declarations
class Application;
class UIRenderer;
class InputHandler;
class StateManager;

// ============================================================================
// Color Palette
// ============================================================================
namespace Colors {
  const auto kBackground = Color::RGB(0x0D, 0x0F, 0x0F);
  const auto kDarkBg = Color::RGB(0x1E, 0x1F, 0x29);
  const auto kGreen = Color::RGB(0x72, 0xF1, 0xB8);
  const auto kBrightGreen = Color::RGB(0x00, 0xFF, 0x9C);
  const auto kDimGreen = Color::RGB(0x66, 0xFF, 0x66);
  const auto kHotPink = Color::RGB(0xFF, 0x2D, 0x95);
  const auto kPink = Color::RGB(0xFF, 0x7E, 0xDB);
  const auto kPurple = Color::RGB(0x9B, 0x5D, 0xF5);
  const auto kCyan = Color::RGB(0x5A, 0xF7, 0x8E);
  const auto kGray = Color::RGB(0x88, 0x88, 0x88);
}

// ============================================================================
// Data Models
// ============================================================================
struct ToolParameter {
  std::string name;
  std::string type;
  std::string description;
  bool required;
  std::string defaultValue;
};

struct Tool {
  std::string name;
  std::string description;
  std::string category;
  std::vector<ToolParameter> parameters;
  std::vector<std::string> examples;
};

struct Config {
  std::string welcomeMessage = "🚀 Hello from FTXUI!";
  std::string inputPlaceholder = "Type here…";
  std::string serverName = "MCP Server";
  std::string serverVersion = "1.0.0";
  int maxHistorySize = 100;
  bool enableLogging = false;
  std::vector<Tool> tools;
};

// ============================================================================
// Communication Mode
// ============================================================================
enum class CommMode {
  STANDALONE,  // Direct terminal interaction
  IPC         // Communicating with Node.js wrapper via JSON-RPC
};

// ============================================================================
// MCP Client - Manages connection to MCP server via TCP
// ============================================================================
class MCPClient {
private:
  ConcurrentQueue<AppEvent> event_queue_;
  std::unique_ptr<TcpClient> tcp_client_;
  std::thread event_processor_thread_;
  bool running_ = false;
  std::string host_;
  int port_;

public:
  MCPClient(const std::string& host = "127.0.0.1", int port = 4000) 
    : host_(host), port_(port) {}
  
  ~MCPClient() {
    Stop();
  }
  
  bool Connect() {
    SPDLOG_INFO("Connecting to TCP server at {}:{}", host_, port_);
    
    tcp_client_ = std::make_unique<TcpClient>(event_queue_);
    
    // Start event processor thread before attempting connection
    running_ = true;
    event_processor_thread_ = std::thread([this]() { ProcessEvents(); });
    
    // Start TCP client connection attempts
    tcp_client_->start(host_, port_);
    
    // Wait a bit to see if initial connection succeeds
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    if (tcp_client_->is_connected()) {
      SPDLOG_INFO("Connected to TCP server");
      return true;
    } else {
      SPDLOG_WARN("Initial connection failed, will keep retrying in background");
      return false;
    }
  }
  
  bool IsConnected() const {
    return tcp_client_ && tcp_client_->is_connected();
  }
  
  void Stop() {
    SPDLOG_INFO("Stopping MCP Server");
    running_ = false;
    
    if (tcp_client_) {
      tcp_client_->stop();
    }
    
    if (event_processor_thread_.joinable()) {
      event_processor_thread_.join();
    }
  }
  
  void SendRequest(const std::string& json) {
    if (!tcp_client_ || !tcp_client_->is_connected()) {
      SPDLOG_ERROR("Cannot send request - not connected");
      return;
    }
    
    SPDLOG_INFO("Sending to server: {}", json);
    tcp_client_->send_message(json);
  }
  
private:
  void ProcessEvents() {
    while (running_) {
      AppEvent event;
      if (event_queue_.pop(event, std::chrono::milliseconds(100))) {
        switch (event.type) {
          case EventType::Connected:
            SPDLOG_INFO("TCP connection established: {}", event.data);
            break;
          case EventType::ConnectionFailed:
            SPDLOG_WARN("TCP connection failed: {}", event.data);
            break;
          case EventType::ConnectionLost:
            SPDLOG_WARN("TCP connection lost: {}", event.data);
            break;
          case EventType::MessageReceived:
            SPDLOG_DEBUG("Received from server: {}", event.data);
            // TODO: Parse JSON and handle responses
            break;
          default:
            break;
        }
      }
    }
    SPDLOG_INFO("Event processor thread exiting");
  }
};

// ============================================================================
// Log Entry for conversation history
// ============================================================================
struct LogEntry {
  enum Type { USER, SYSTEM, RESPONSE, ERROR };
  Type type;
  std::string content;
  std::chrono::system_clock::time_point timestamp;
};

// ============================================================================
// State Manager - Single source of truth for application state
// ============================================================================
class StateManager {
public:
  enum class AppState {
    RUNNING,
    EXIT_REQUESTED,
    EXITING
  };

  enum class DisplayMode {
    NORMAL,  // Normal chat/log view
    HELP,
    LIST,
    TOOL_INFO,
    UNKNOWN_COMMAND,
    EXIT_WARNING,
    EXIT_MESSAGE,
    DUMP_SUCCESS,
    CHAT_NOT_IMPLEMENTED,
    MCP_ERROR
  };

private:
  AppState app_state_ = AppState::RUNNING;
  DisplayMode display_mode_ = DisplayMode::NORMAL;
  
  std::chrono::steady_clock::time_point last_ctrl_c_time_;
  bool ctrl_c_pending_ = false;
  
  std::vector<std::string> command_history_;
  std::vector<LogEntry> conversation_log_;
  Tool* selected_tool_ = nullptr;
  int max_history_size_ = 100;
  int scroll_position_ = 0;

public:
  // Clean interface for state transitions
  void RequestExit() {
    if (app_state_ == AppState::RUNNING) {
      app_state_ = AppState::EXIT_REQUESTED;
      display_mode_ = DisplayMode::EXIT_MESSAGE;
    }
  }

  void ConfirmExit() {
    app_state_ = AppState::EXITING;
  }

  bool IsExiting() const {
    return app_state_ == AppState::EXITING;
  }

  bool IsExitRequested() const {
    return app_state_ == AppState::EXIT_REQUESTED;
  }

  void HandleCtrlC() {
    auto now = std::chrono::steady_clock::now();
    
    if (ctrl_c_pending_) {
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ctrl_c_time_);
      SPDLOG_DEBUG("Second Ctrl+C received, elapsed: {}ms", elapsed.count());
      if (elapsed.count() < 2000) {
        SPDLOG_INFO("Double Ctrl+C detected - requesting exit");
        RequestExit();
        return;
      }
    }
    
    SPDLOG_DEBUG("First Ctrl+C received");
    ctrl_c_pending_ = true;
    last_ctrl_c_time_ = now;
    display_mode_ = DisplayMode::EXIT_WARNING;
  }

  void ResetCtrlC() {
    ctrl_c_pending_ = false;
  }

  bool IsCtrlCPending() const {
    if (!ctrl_c_pending_) return false;
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ctrl_c_time_);
    return elapsed.count() < 2000;
  }

  // Display mode management
  void SetDisplayMode(DisplayMode mode) { 
    SPDLOG_DEBUG("Setting display mode from {} to {}", static_cast<int>(display_mode_), static_cast<int>(mode));
    display_mode_ = mode; 
  }
  DisplayMode GetDisplayMode() const { return display_mode_; }
  
  // Tool selection
  void SelectTool(Tool* tool) { 
    selected_tool_ = tool;
    display_mode_ = tool ? DisplayMode::TOOL_INFO : DisplayMode::UNKNOWN_COMMAND;
  }
  Tool* GetSelectedTool() const { return selected_tool_; }
  
  // Command history
  void AddToHistory(const std::string& command) {
    command_history_.push_back(command);
    if (command_history_.size() > static_cast<size_t>(max_history_size_)) {
      command_history_.erase(command_history_.begin());
    }
  }
  
  const std::vector<std::string>& GetHistory() const { return command_history_; }
  void SetMaxHistorySize(int size) { max_history_size_ = size; }
  
  // Conversation log management
  void AddLogEntry(LogEntry::Type type, const std::string& content) {
    conversation_log_.push_back(LogEntry{
      type, 
      content, 
      std::chrono::system_clock::now()
    });
    // Auto-scroll to bottom when new entry is added
    scroll_position_ = std::max(0, static_cast<int>(conversation_log_.size()) - 10);
  }
  
  const std::vector<LogEntry>& GetConversationLog() const { return conversation_log_; }
  
  void ClearConversationLog() { conversation_log_.clear(); }
  
  int GetScrollPosition() const { return scroll_position_; }
  void SetScrollPosition(int pos) { 
    scroll_position_ = std::max(0, std::min(pos, static_cast<int>(conversation_log_.size()) - 1));
  }
};

// ============================================================================
// Input Handler - Processes user input and updates state
// ============================================================================
class InputHandler {
private:
  StateManager& state_;
  std::vector<Tool>& tools_;
  UIRenderer* renderer_ = nullptr;
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
  InputHandler(StateManager& state, std::vector<Tool>& tools) 
    : state_(state), tools_(tools) {}
    
  void SetRenderer(UIRenderer* renderer) {
    renderer_ = renderer;
  }
  
  void SetCommMode(CommMode mode) {
    comm_mode_ = mode;
  }
  
  void SetMCPClient(MCPClient* client) {
    mcp_client_ = client;
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
    
    // Simple JSON construction for IPC
    std::stringstream json;
    json << "{\"type\":\"chat\",\"id\":" << ++message_id_ 
         << ",\"content\":\"" << EscapeJSON(message) << "\"}";
    
    SPDLOG_INFO("Sending chat JSON: {}", json.str());
    mcp_client_->SendRequest(json.str());
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
    
    SPDLOG_INFO("Processing command: '{}' (mode: {})", command, 
                comm_mode_ == CommMode::IPC ? "IPC" : "STANDALONE");
    state_.AddToHistory(command);
    
    // Add user input to log
    state_.AddLogEntry(LogEntry::USER, command);
    
    // Check if it's a slash command
    if (command[0] == '/') {
      std::string cmd = command.substr(1); // Remove the '/'
      
      if (cmd == "help" || cmd == "h") {
        SPDLOG_DEBUG("Showing help");
        state_.AddLogEntry(LogEntry::SYSTEM, "Available commands:\n/help - Show this help\n/tools - List available tools\n/servers - Show connected servers\n/exit - Exit the application");
      } else if (cmd == "tools") {
        SPDLOG_DEBUG("Listing MCP tools");
        if (comm_mode_ == CommMode::IPC) {
          if (!mcp_client_ || !mcp_client_->IsConnected()) {
            SPDLOG_ERROR("MCP server not connected");
            state_.AddLogEntry(LogEntry::ERROR, "Cannot list tools: MCP server not connected");
          } else {
            SendMCPRequest("tools/list", "{}");
            state_.AddLogEntry(LogEntry::SYSTEM, "Requesting tool list...");
          }
        } else {
          state_.AddLogEntry(LogEntry::SYSTEM, "No tools available in standalone mode");
        }
      } else if (cmd == "servers") {
        SPDLOG_DEBUG("Showing connected servers");
        if (mcp_client_ && mcp_client_->IsConnected()) {
          state_.AddLogEntry(LogEntry::SYSTEM, "Connected to TCP server at 127.0.0.1:4000");
        } else {
          state_.AddLogEntry(LogEntry::SYSTEM, "No servers connected");
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
          state_.AddLogEntry(LogEntry::SYSTEM, "Executing tool: " + toolName);
        } else {
          state_.AddLogEntry(LogEntry::ERROR, "Tool execution not available in standalone mode");
        }
      } else if (cmd == "clear") {
        state_.ClearConversationLog();
        state_.AddLogEntry(LogEntry::SYSTEM, "Conversation cleared");
      } else if (cmd == "dump") {
        SPDLOG_DEBUG("Dump screen requested");
        DumpScreen();
      } else if (cmd == "exit" || cmd == "quit" || cmd == "q") {
        SPDLOG_INFO("Exit command received");
        state_.RequestExit();
      } else {
        SPDLOG_WARN("Unknown command: /{}", cmd);
        state_.AddLogEntry(LogEntry::ERROR, "Unknown command: /" + cmd);
      }
    } else {
      // Non-slash commands go to chatbot
      SPDLOG_INFO("Chatbot message: {}", command);
      
      if (comm_mode_ == CommMode::IPC) {
        SPDLOG_INFO("Sending chat message to MCP server");
        SendChatMessage(command);
        state_.AddLogEntry(LogEntry::SYSTEM, "[Awaiting response...]");
      } else {
        SPDLOG_INFO("In standalone mode - showing not implemented");
        state_.AddLogEntry(LogEntry::RESPONSE, "Chat functionality requires connection to MCP server");
      }
    }
  }

  bool HandleEvent(const Event& event) {
    if (event == Event::CtrlC) {
      state_.HandleCtrlC();
      return true;
    }
    
    // Reset Ctrl+C on other input
    if (event.is_character()) {
      state_.ResetCtrlC();
    }
    
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
    const auto& log = state_.GetConversationLog();
    
    for (const auto& entry : log) {
      Element line;
      
      // Format timestamp
      auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
      char time_str[20];
      std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));
      
      switch (entry.type) {
        case LogEntry::USER:
          line = hbox({
            text("[") | color(Colors::kGray),
            text(time_str) | color(Colors::kGray),
            text("] ") | color(Colors::kGray),
            text("You: ") | bold | color(Colors::kCyan),
            text(entry.content) | color(Colors::kGreen)
          });
          break;
        case LogEntry::SYSTEM:
          line = hbox({
            text("[") | color(Colors::kGray),
            text(time_str) | color(Colors::kGray),
            text("] ") | color(Colors::kGray),
            text("System: ") | bold | color(Colors::kPurple),
            text(entry.content) | color(Colors::kDimGreen)
          });
          break;
        case LogEntry::RESPONSE:
          line = hbox({
            text("[") | color(Colors::kGray),
            text(time_str) | color(Colors::kGray),
            text("] ") | color(Colors::kGray),
            text("Assistant: ") | bold | color(Colors::kPink),
            text(entry.content) | color(Colors::kGray)
          });
          break;
        case LogEntry::ERROR:
          line = hbox({
            text("[") | color(Colors::kGray),
            text(time_str) | color(Colors::kGray),
            text("] ") | color(Colors::kGray),
            text("Error: ") | bold | color(Colors::kHotPink),
            text(entry.content) | color(Colors::kHotPink)
          });
          break;
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
  
  ScreenInteractive screen_;
  Closure exit_closure_;
  
  std::string user_input_;
  Component input_component_;
  CommMode comm_mode_ = CommMode::STANDALONE;

public:
  Application() : screen_(ScreenInteractive::Fullscreen()) {
    // Clear screen on startup
    std::cout << "\033[2J\033[H" << std::flush;
    
    // Load configuration
    LoadConfig("./config.json");
    
    // Initialize components
    state_.SetMaxHistorySize(config_.maxHistorySize);
    renderer_ = std::make_unique<UIRenderer>(config_, state_, config_.tools);
    input_handler_ = std::make_unique<InputHandler>(state_, config_.tools);
    input_handler_->SetRenderer(renderer_.get());
    
    // Setup screen
    screen_.ForceHandleCtrlC(false);
    exit_closure_ = screen_.ExitLoopClosure();
    
    // Create input component
    input_component_ = Input(&user_input_, config_.inputPlaceholder);
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
        SPDLOG_INFO("TCP client connected successfully");
      }
    }
  }

  void Run() {
    int scroll_position = 0;
    
    // Create scrollable middle area
    auto middle_renderer = Renderer([this, &scroll_position] {
      // Update connection status
      if (mcp_client_) {
        renderer_->SetConnectionStatus(mcp_client_->IsConnected());
      }
      
      // Get conversation log
      auto log_content = renderer_->RenderConversationLog();
      
      // Apply scroll position
      return log_content 
        | frame 
        | focusPosition(0, scroll_position)
        | vscroll_indicator 
        | flex;
    });
    
    // Catch events for scrolling
    auto middle = CatchEvent(middle_renderer, [this, &scroll_position](Event event) {
      const auto& log = state_.GetConversationLog();
      int max_scroll = std::max(0, static_cast<int>(log.size()) - 10);
      
      if (event == Event::ArrowUp || event == Event::PageUp) {
        scroll_position = std::max(0, scroll_position - 1);
        return true;
      }
      if (event == Event::ArrowDown || event == Event::PageDown) {
        scroll_position = std::min(max_scroll, scroll_position + 1);
        return true;
      }
      if (event == Event::Home) {
        scroll_position = 0;
        return true;
      }
      if (event == Event::End) {
        scroll_position = max_scroll;
        return true;
      }
      return false;
    });
    
    // Create header
    auto header = Renderer([this] {
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
        connection_status,
        text(" ") | color(Colors::kPurple)
      }) | border | color(Colors::kPurple);
    });
    
    // Create vertical layout
    auto layout = Container::Vertical({
      header,
      middle,
      input_component_
    });
    
    // Final renderer that composes everything
    auto main_component = Renderer(layout, [this, &header, &middle, &scroll_position] {
      // Check exit condition
      if (state_.IsExitRequested()) {
        state_.ConfirmExit();
        screen_.Post([this] { exit_closure_(); });
      }
      
      // Auto-scroll to bottom on new messages
      const auto& log = state_.GetConversationLog();
      if (!log.empty()) {
        scroll_position = std::max(0, static_cast<int>(log.size()) - 10);
      }
      
      return vbox({
        header->Render() | size(HEIGHT, EQUAL, 3),
        middle->Render() | flex,
        hbox({
          text(" ▶ ") | color(Colors::kHotPink),
          input_component_->Render() | color(Colors::kGreen)
        }) | size(HEIGHT, EQUAL, 1)
      }) | bgcolor(Colors::kBackground);
    });

    // Handle events
    auto event_handler = CatchEvent(main_component, [this](Event event) {
      // Let input handler process first
      if (input_handler_->HandleEvent(event)) {
        return true;
      }
      
      // Handle Enter key
      if (event == Event::Return && !user_input_.empty()) {
        SPDLOG_DEBUG("Enter pressed with input: {}", user_input_);
        std::string command = user_input_;
        user_input_.clear();
        
        // Process command directly
        input_handler_->ProcessCommand(command);
        
        return true;
      }
      
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
