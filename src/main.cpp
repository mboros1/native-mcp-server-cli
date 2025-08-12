#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/fmt/chrono.h>

#include "event.hpp"
#include "tcp_client.hpp"
#include "include/types_ui.hpp"
#include "network/mcp_client.hpp"
#include "core/state_manager.hpp"
#include "components/conversation_log_manager.hpp"
#include "components/input_with_history.hpp"
#include "components/ui_renderer.hpp"
#include "core/input_handler.hpp"
#include "adapters/ftxui_event_adapter.hpp"
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


// ============================================================================
// State Manager - Single source of truth for application state
// ============================================================================





// ============================================================================
// Application - Main controller that coordinates everything
// ============================================================================
class Application {
private:
  // Log entry rendering configuration
  struct LogEntryStyle {
    std::string label;
    int prefix_width;
    Color label_color;
    Color content_color;
  };
  
  static inline const std::map<LogEntryType, LogEntryStyle> log_styles_ = {
    {LogEntryType::USER,     {"You: ",      24, Colors::kCyan,     Colors::kGreen}},
    {LogEntryType::SYSTEM,   {"System: ",   27, Colors::kPurple,   Colors::kDimGreen}},
    {LogEntryType::RESPONSE, {"Assistant: ", 30, Colors::kPink,     Colors::kGray}},
    {LogEntryType::ERROR,    {"Error: ",    26, Colors::kHotPink,  Colors::kHotPink}},
    {LogEntryType::BLOCKED,  {"Blocked: ",  28, Colors::kPurple,   Colors::kPurple}}
  };
  
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
  
  // Helper method to render a log entry
  Element RenderLogEntry(const LogEntry& entry, const char* time_str) {
    auto it = log_styles_.find(entry.type);
    if (it == log_styles_.end()) {
      // Fallback for unknown types
      return text("Unknown entry type");
    }
    
    const auto& style = it->second;
    int terminal_width = Terminal::Size().dimx;
    int content_width = terminal_width - style.prefix_width;
    
    return hbox({
      text("[") | color(Colors::kGray),
      text(time_str) | color(Colors::kGray),
      text("] ") | color(Colors::kGray),
      text(style.label) | bold | color(style.label_color),
      paragraph(entry.content) | color(style.content_color) | size(WIDTH, LESS_THAN, content_width)
    });
  }
  
  // Helper method to render the event log
  Element RenderEventLog() {
    Elements log_elements;
    const auto& log = state_.GetEventLog();
    
    // Render all log entries
    for (const auto& entry : log) {
      // Format timestamp
      auto time_t = std::chrono::system_clock::to_time_t(entry.timestamp);
      char time_str[20];
      std::strftime(time_str, sizeof(time_str), "%H:%M:%S", std::localtime(&time_t));
      
      // Use our helper method to render the log entry
      log_elements.push_back(RenderLogEntry(entry, time_str));
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
  }

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
    
    // Set up notification callback to trigger UI refresh when async responses arrive
    input_handler_->SetNotificationCallback([this](LogEntryType, const std::string&) {
      // Trigger UI refresh when new log entries arrive
      log_manager_.OnNewMessage();
    });
    
    // Create conversation log component using Renderer
    auto event_log_renderer = Renderer([this] {
      return RenderEventLog();
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
        
        // Set up callback to handle response IDs for JSON-RPC correlation
        mcp_client_->SetIdCallback([this](int id) {
          input_handler_->OnResponseReceived(id);
        });
        
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
          } else if (type == "TOOL_EVENT") {
            // Handle tool-related events (tool calls, results, errors, info)
            // Write to chat history so it's persisted
            state_.WriteToChatHistory("system", content);
            input_handler_->AddLogEntryWithNotification(LogEntryType::SYSTEM, content);
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
      // Convert ftxui::Event to app::Event and let input handler process first
      app::Event appEvent = app::FTXUIEventAdapter::Convert(event);
      if (input_handler_->HandleEvent(appEvent)) {
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
