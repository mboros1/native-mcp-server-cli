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
    HELP,
    LIST,
    TOOL_INFO,
    UNKNOWN_COMMAND,
    EXIT_WARNING,
    EXIT_MESSAGE
  };

private:
  AppState app_state_ = AppState::RUNNING;
  DisplayMode display_mode_ = DisplayMode::HELP;
  
  std::chrono::steady_clock::time_point last_ctrl_c_time_;
  bool ctrl_c_pending_ = false;
  
  std::vector<std::string> command_history_;
  Tool* selected_tool_ = nullptr;
  int max_history_size_ = 100;

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
      if (elapsed.count() < 2000) {
        RequestExit();
        return;
      }
    }
    
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
  void SetDisplayMode(DisplayMode mode) { display_mode_ = mode; }
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
};

// ============================================================================
// Input Handler - Processes user input and updates state
// ============================================================================
class InputHandler {
private:
  StateManager& state_;
  std::vector<Tool>& tools_;
  
  Tool* FindTool(const std::string& name) {
    for (auto& tool : tools_) {
      if (tool.name == name) {
        return &tool;
      }
    }
    return nullptr;
  }

public:
  InputHandler(StateManager& state, std::vector<Tool>& tools) 
    : state_(state), tools_(tools) {}

  void ProcessCommand(const std::string& command) {
    if (command.empty()) return;
    
    state_.AddToHistory(command);
    
    if (command == "help") {
      state_.SetDisplayMode(StateManager::DisplayMode::HELP);
      state_.SelectTool(nullptr);
    } else if (command == "list") {
      state_.SetDisplayMode(StateManager::DisplayMode::LIST);
      state_.SelectTool(nullptr);
    } else if (command == "exit" || command == "quit") {
      state_.RequestExit();
    } else {
      Tool* tool = FindTool(command);
      state_.SelectTool(tool);
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

  Elements RenderHelp() const {
    return {
      text("▶ COMMANDS") | bold | color(Colors::kBrightGreen),
      text(""),
      hbox(text("  help     ") | color(Colors::kCyan), 
           text("→ Show this help message") | color(Colors::kDimGreen)),
      hbox(text("  list     ") | color(Colors::kCyan), 
           text("→ List all available tools") | color(Colors::kDimGreen)),
      hbox(text("  <tool>   ") | color(Colors::kCyan), 
           text("→ Show detailed information about a specific tool") | color(Colors::kDimGreen)),
      hbox(text("  exit     ") | color(Colors::kCyan), 
           text("→ Exit the application") | color(Colors::kDimGreen)),
      text(""),
      separator() | color(Colors::kPurple),
      text(""),
      text("Type 'list' to see all available tools or type a tool name directly.") | color(Colors::kGray)
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

  Element Render() const {
    // Exit immediately if we're exiting
    if (state_.IsExiting()) {
      return vbox({
        text("◆ SEE YOU IN THE CYBER WORLD ◆") | bold | color(Colors::kBrightGreen) | center
      }) | bgcolor(Colors::kBackground);
    }

    // Header
    auto header = vbox({
      text(""),
      hbox(
        text("▓▒░ ") | color(Colors::kPurple),
        text(config_.welcomeMessage) | bold | color(Colors::kBrightGreen),
        text(" ░▒▓") | color(Colors::kPurple)
      ) | center,
      text(config_.serverName + " v" + config_.serverVersion) | color(Colors::kGray) | center,
      text(""),
      separator() | color(Colors::kPurple),
    });

    // Main content based on state
    Elements main_content;
    switch (state_.GetDisplayMode()) {
      case StateManager::DisplayMode::HELP:
        main_content = RenderHelp();
        break;
      case StateManager::DisplayMode::LIST:
        main_content = RenderToolList();
        break;
      case StateManager::DisplayMode::TOOL_INFO:
        if (auto* tool = state_.GetSelectedTool()) {
          main_content = RenderToolInfo(*tool);
        }
        break;
      case StateManager::DisplayMode::UNKNOWN_COMMAND:
        main_content.push_back(text("⚠ Unknown command. Type 'help' for available commands.") 
                             | color(Colors::kHotPink));
        break;
      case StateManager::DisplayMode::EXIT_WARNING:
        main_content.push_back(text("⚠ Press Ctrl+C again to exit") 
                             | bold | color(Colors::kHotPink) | center);
        break;
      case StateManager::DisplayMode::EXIT_MESSAGE:
        main_content.push_back(text("◆ SEE YOU IN THE CYBER WORLD ◆") 
                             | bold | color(Colors::kBrightGreen) | center);
        break;
    }

    auto content_area = vbox(main_content) | flex;

    // Info ticker
    std::string ticker_text;
    if (state_.IsCtrlCPending()) {
      ticker_text = "Press Ctrl+C again to exit";
    } else if (!state_.GetHistory().empty()) {
      ticker_text = "Last command: " + state_.GetHistory().back();
    } else {
      ticker_text = "Type 'help' for commands • Ctrl+C twice to exit";
    }
    
    auto ticker = hbox({
      text(" ◉ ") | color(Colors::kCyan),
      text(ticker_text) | color(Colors::kGray),
    }) | center;

    return vbox({
      header,
      content_area,
      separator() | color(Colors::kPurple),
      text(""),
      ticker,
      text(""),
    }) | bgcolor(Colors::kBackground);
  }
};

// ============================================================================
// Application - Main controller that coordinates everything
// ============================================================================
class Application {
private:
  Config config_;
  StateManager state_;
  std::unique_ptr<UIRenderer> renderer_;
  std::unique_ptr<InputHandler> input_handler_;
  
  ScreenInteractive screen_;
  Closure exit_closure_;
  
  std::string user_input_;
  Component input_component_;

public:
  Application() : screen_(ScreenInteractive::TerminalOutput()) {
    // Clear screen on startup
    std::cout << "\033[2J\033[H" << std::flush;
    
    // Load configuration
    LoadConfig("../config.json");
    
    // Initialize components
    state_.SetMaxHistorySize(config_.maxHistorySize);
    renderer_ = std::make_unique<UIRenderer>(config_, state_, config_.tools);
    input_handler_ = std::make_unique<InputHandler>(state_, config_.tools);
    
    // Setup screen
    screen_.ForceHandleCtrlC(false);
    exit_closure_ = screen_.ExitLoopClosure();
    
    // Create input component
    input_component_ = Input(&user_input_, config_.inputPlaceholder);
  }

  void Run() {
    // Create main UI component
    auto main_component = Renderer(input_component_, [this] {
      // Check exit condition
      if (state_.IsExitRequested()) {
        state_.ConfirmExit();
        screen_.Post([this] { exit_closure_(); });
      }
      
      // Render UI
      auto ui = renderer_->Render();
      
      // Add input
      auto input_section = vbox({
        hbox(
          text("▶ ") | color(Colors::kHotPink),
          input_component_->Render() | color(Colors::kGreen)
        )
      });
      
      return vbox({ui, input_section}) | border | borderDouble | color(Colors::kPurple);
    });

    // Handle events
    auto event_handler = CatchEvent(main_component, [this](Event event) {
      // Let input handler process first
      if (input_handler_->HandleEvent(event)) {
        return true;
      }
      
      // Handle Enter key
      if (event == Event::Return && !user_input_.empty()) {
        input_handler_->ProcessCommand(user_input_);
        user_input_.clear();
        return true;
      }
      
      return false;
    });

    // Run the main loop
    screen_.Loop(event_handler);
  }

private:
  void LoadConfig(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
      std::cerr << "Warning: Config file not found at " << filepath << ", using defaults\n";
      return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json_str = buffer.str();
    
    dom::parser parser;
    dom::element doc;
    
    auto error = parser.parse(json_str).get(doc);
    if (error) {
      std::cerr << "Warning: Failed to parse config file: " << error << ", using defaults\n";
      return;
    }
    
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
      }
    } catch (const std::exception& e) {
      std::cerr << "Warning: Error reading config values: " << e.what() << "\n";
    }
  }
};

// ============================================================================
// Main
// ============================================================================
int main() {
  try {
    Application app;
    app.Run();
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << "\n";
    return 1;
  }
  
  return 0;
}