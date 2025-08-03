#include <ftxui/component/component.hpp>          // Input, Renderer
#include <ftxui/component/screen_interactive.hpp> // ScreenInteractive
#include <ftxui/dom/elements.hpp>                 // text, vbox, hbox, separator
#include <ftxui/component/event.hpp>              // Event
#include <simdjson.h>
#include <atomic_queue/atomic_queue.h>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <map>
#include <chrono>
#include <atomic>
#include <memory>

using namespace ftxui;
using namespace simdjson;

// Event types for our event handler
enum class EventType {
  CTRL_C,
  COMMAND,
  OTHER
};

struct AppEvent {
  EventType type;
  std::chrono::steady_clock::time_point timestamp;
  std::string data;
};

// Event handler class
class EventHandler {
private:
  atomic_queue::AtomicQueue<AppEvent, 64> event_queue;
  std::chrono::steady_clock::time_point last_ctrl_c_time;
  const std::chrono::milliseconds ctrl_c_timeout{2000};
  std::atomic<bool> should_exit{false};
  
public:
  void pushEvent(const AppEvent& event) {
    event_queue.push(event);
  }
  
  bool shouldExit() const {
    return should_exit.load();
  }
  
  // Process all pending events and return current display state
  std::string processEvents(std::string current_display) {
    AppEvent event;
    int ctrl_c_count = 0;
    std::chrono::steady_clock::time_point latest_ctrl_c_time;
    
    // Process all events in queue
    while (event_queue.try_pop(event)) {
      if (event.type == EventType::CTRL_C) {
        ctrl_c_count++;
        latest_ctrl_c_time = event.timestamp;
      }
    }
    
    // Handle Ctrl+C logic
    if (ctrl_c_count > 0) {
      auto now = std::chrono::steady_clock::now();
      
      // Check if we have a recent Ctrl+C
      if (last_ctrl_c_time != std::chrono::steady_clock::time_point{} &&
          (latest_ctrl_c_time - last_ctrl_c_time) < ctrl_c_timeout) {
        // Two Ctrl+C within timeout - exit
        should_exit.store(true);
        return "exit";
      } else {
        // First Ctrl+C or timeout expired
        last_ctrl_c_time = latest_ctrl_c_time;
        return "ctrl_c_warning";
      }
    }
    
    return current_display;
  }
  
  bool hasRecentCtrlC() const {
    auto now = std::chrono::steady_clock::now();
    return last_ctrl_c_time != std::chrono::steady_clock::time_point{} &&
           (now - last_ctrl_c_time) < ctrl_c_timeout;
  }
};

// Synthwave color palette
namespace Colors {
  const auto kBackground = Color::RGB(0x0D, 0x0F, 0x0F);   // Almost black
  const auto kDarkBg = Color::RGB(0x1E, 0x1F, 0x29);       // Dark synthwave background
  const auto kGreen = Color::RGB(0x72, 0xF1, 0xB8);        // Synthwave green
  const auto kBrightGreen = Color::RGB(0x00, 0xFF, 0x9C);  // Bright phosphor green
  const auto kDimGreen = Color::RGB(0x66, 0xFF, 0x66);     // Readable body text green
  const auto kHotPink = Color::RGB(0xFF, 0x2D, 0x95);      // Neon pink accent
  const auto kPink = Color::RGB(0xFF, 0x7E, 0xDB);         // Soft pink
  const auto kPurple = Color::RGB(0x9B, 0x5D, 0xF5);       // Purple accent
  const auto kCyan = Color::RGB(0x5A, 0xF7, 0x8E);         // Cyberpunk cyan
  const auto kGray = Color::RGB(0x88, 0x88, 0x88);        // Dim gray for less important text
}

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

Config loadConfig(const std::string& filepath) {
  Config config;
  
  // Read file
  std::ifstream file(filepath);
  if (!file.is_open()) {
    std::cerr << "Warning: Config file not found at " << filepath << ", using defaults\n";
    return config;
  }
  
  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string json_str = buffer.str();
  
  // Parse with simdjson
  dom::parser parser;
  dom::element doc;
  
  auto error = parser.parse(json_str).get(doc);
  if (error) {
    std::cerr << "Warning: Failed to parse config file: " << error << ", using defaults\n";
    return config;
  }
  
  // Extract values safely
  try {
    if (doc["ui"]["welcomeMessage"].is_string()) {
      config.welcomeMessage = std::string(doc["ui"]["welcomeMessage"].get_string().value());
    }
    if (doc["ui"]["inputPlaceholder"].is_string()) {
      config.inputPlaceholder = std::string(doc["ui"]["inputPlaceholder"].get_string().value());
    }
    if (doc["server"]["name"].is_string()) {
      config.serverName = std::string(doc["server"]["name"].get_string().value());
    }
    if (doc["server"]["version"].is_string()) {
      config.serverVersion = std::string(doc["server"]["version"].get_string().value());
    }
    if (doc["settings"]["maxHistorySize"].is_int64()) {
      config.maxHistorySize = int(doc["settings"]["maxHistorySize"].get_int64().value());
    }
    if (doc["settings"]["enableLogging"].is_bool()) {
      config.enableLogging = doc["settings"]["enableLogging"].get_bool().value();
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
            if (value["default"].is_string()) {
              param.defaultValue = std::string(value["default"].get_string().value());
            } else if (value["default"].is_number()) {
              param.defaultValue = std::to_string(value["default"].get_int64().value());
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
        
        config.tools.push_back(t);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Warning: Error reading config values: " << e.what() << "\n";
  }
  
  return config;
}

// Helper function to find tool by name
Tool* findTool(std::vector<Tool>& tools, const std::string& name) {
  for (auto& tool : tools) {
    if (tool.name == name) {
      return &tool;
    }
  }
  return nullptr;
}

// Helper function to generate tool info display
Elements generateToolInfo(const Tool& tool) {
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
      if (!param.defaultValue.empty()) {
        elements.push_back(text("    Default: " + param.defaultValue) | color(Colors::kGray));
      }
    }
  }
  
  if (!tool.examples.empty()) {
    elements.push_back(separator() | color(Colors::kPurple));
    elements.push_back(text("▸ Examples") | bold | color(Colors::kCyan));
    for (const auto& example : tool.examples) {
      elements.push_back(text("  " + example) | color(Colors::kBrightGreen));
    }
  }
  
  return elements;
}

// Helper function to generate help text
Elements generateHelp() {
  Elements elements;
  
  elements.push_back(text("▶ COMMANDS") | bold | color(Colors::kBrightGreen));
  elements.push_back(text(""));
  elements.push_back(hbox(text("  help     ") | color(Colors::kCyan), text("→ Show this help message") | color(Colors::kDimGreen)));
  elements.push_back(hbox(text("  list     ") | color(Colors::kCyan), text("→ List all available tools") | color(Colors::kDimGreen)));
  elements.push_back(hbox(text("  <tool>   ") | color(Colors::kCyan), text("→ Show detailed information about a specific tool") | color(Colors::kDimGreen)));
  elements.push_back(hbox(text("  exit     ") | color(Colors::kCyan), text("→ Exit the application") | color(Colors::kDimGreen)));
  elements.push_back(hbox(text("  quit     ") | color(Colors::kCyan), text("→ Exit the application") | color(Colors::kDimGreen)));
  elements.push_back(text(""));
  elements.push_back(separator() | color(Colors::kPurple));
  elements.push_back(text(""));
  elements.push_back(text("Type 'list' to see all available tools or type a tool name directly.") | color(Colors::kGray));
  
  return elements;
}

// Helper function to generate tool list
Elements generateToolList(const std::vector<Tool>& tools) {
  Elements elements;
  
  elements.push_back(text("◉ AVAILABLE TOOLS") | bold | color(Colors::kBrightGreen));
  elements.push_back(text(""));
  
  std::map<std::string, std::vector<const Tool*>> toolsByCategory;
  for (const auto& tool : tools) {
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
    elements.push_back(text(""));  // Empty line between categories
  }
  
  elements.push_back(separator() | color(Colors::kPurple));
  elements.push_back(text(""));
  elements.push_back(text("Type any tool name to see detailed information.") | color(Colors::kGray));
  
  return elements;
}

int main() {
  // Clear the screen before starting
  std::cout << "\033[2J\033[H" << std::flush;
  
  // Load configuration
  Config config = loadConfig("../config.json");
  
  std::string user_text;
  std::vector<std::string> submitted_texts;
  std::string current_display = "help";  // What to show in the output area
  Tool* selected_tool = nullptr;
  
  // Event handler
  auto event_handler = std::make_shared<EventHandler>();

  // 1. Set up screen and exit handler ----------------------------------------
  auto screen = ScreenInteractive::TerminalOutput();
  
  // Disable default Ctrl+C handling so we can handle it ourselves
  screen.ForceHandleCtrlC(false);
  
  // Get the exit closure
  auto exit_closure = screen.ExitLoopClosure();
  
  // 2. Core widget -----------------------------------------------------------
  auto input = Input(&user_text, config.inputPlaceholder);
  
  // 3. Capture Enter key events ----------------------------------------------
  auto input_with_enter = CatchEvent(input, [&](Event event) {
    // Handle Ctrl+C
    if (event == Event::CtrlC) {
      auto now = std::chrono::steady_clock::now();
      
      if (ctrl_c_pressed.load() && (now - last_ctrl_c_time) < ctrl_c_timeout) {
        // Second Ctrl+C within timeout - exit
        current_display = "exit";
      } else {
        // First Ctrl+C or timeout expired
        ctrl_c_pressed.store(true);
        last_ctrl_c_time = now;
        current_display = "ctrl_c_warning";
      }
      return true;  // Mark event as handled to prevent default exit
    }
    
    // Reset Ctrl+C state on any other input
    if (event.is_character() && event != Event::CtrlC) {
      ctrl_c_pressed.store(false);
    }
    
    if (event == Event::Return) {
      // Handle Enter key - submit the text
      if (!user_text.empty()) {
        std::string command = user_text;
        submitted_texts.push_back(command);
        
        // Parse command
        if (command == "help") {
          current_display = "help";
          selected_tool = nullptr;
        } else if (command == "list") {
          current_display = "list";
          selected_tool = nullptr;
        } else if (command == "exit" || command == "quit") {
          // Exit will be handled by the screen loop
          current_display = "exit";
        } else {
          // Try to find a tool with this name
          selected_tool = findTool(config.tools, command);
          if (selected_tool) {
            current_display = "tool";
          } else {
            current_display = "unknown";
          }
        }
        
        // Limit history size based on config
        if (submitted_texts.size() > static_cast<size_t>(config.maxHistorySize)) {
          submitted_texts.erase(submitted_texts.begin());
        }
        user_text.clear();  // Clear input after submission
      }
      return true;  // Event handled
    }
    return false;  // Let other events pass through
  });

  // 3. Glue: turn the widget into a renderable tree --------------------------
  auto ui = Renderer(input_with_enter, [&] {
    // Check if we should exit on every render
    if (current_display == "exit") {
      exit_closure();
    }
    
    // Header section
    auto header = vbox({
      text(""),
      hbox(
        text("▓▒░ ") | color(Colors::kPurple),
        text(config.welcomeMessage) | bold | color(Colors::kBrightGreen),
        text(" ░▒▓") | color(Colors::kPurple)
      ) | center,
      text(config.serverName + " v" + config.serverVersion) | color(Colors::kGray) | center,
      text(""),
      separator() | color(Colors::kPurple),
    });
    
    // Main content area
    Elements main_content;
    if (current_display == "help") {
      auto helpElements = generateHelp();
      main_content.insert(main_content.end(), helpElements.begin(), helpElements.end());
    } else if (current_display == "list") {
      auto listElements = generateToolList(config.tools);
      main_content.insert(main_content.end(), listElements.begin(), listElements.end());
    } else if (current_display == "tool" && selected_tool) {
      auto toolElements = generateToolInfo(*selected_tool);
      main_content.insert(main_content.end(), toolElements.begin(), toolElements.end());
    } else if (current_display == "unknown") {
      main_content.push_back(text("⚠ Unknown command. Type 'help' for available commands.") | color(Colors::kHotPink));
    } else if (current_display == "ctrl_c_warning") {
      main_content.push_back(text("⚠ Press Ctrl+C again to exit") | bold | color(Colors::kHotPink) | center);
    } else if (current_display == "exit") {
      main_content.push_back(text("◆ SEE YOU IN THE CYBER WORLD ◆") | bold | color(Colors::kBrightGreen) | center);
    }
    
    // Create scrollable content area
    auto content_area = vbox(main_content) | flex;
    
    // Input section
    auto input_section = vbox({
      separator() | color(Colors::kPurple),
      text(""),
      hbox(
        text("▶ ") | color(Colors::kHotPink),
        input->Render() | color(Colors::kGreen)
      ),
      text(""),
    });
    
    // Info ticker
    std::string ticker_text = "";
    if (ctrl_c_pressed.load()) {
      ticker_text = "Press Ctrl+C again to exit";
    } else if (!submitted_texts.empty()) {
      ticker_text = "Last command: " + submitted_texts.back();
    } else {
      ticker_text = "Type 'help' for commands • Ctrl+C twice to exit";
    }
    
    auto ticker = hbox({
      text(" ◉ ") | color(Colors::kCyan),
      text(ticker_text) | color(Colors::kGray),
    }) | center;
    
    // Combine all sections
    return vbox({
      header,
      content_area,
      input_section,
      ticker,
      text(""), // Bottom padding
    }) | bgcolor(Colors::kBackground) | border | borderDouble | color(Colors::kPurple);
  });

  // 4. Hand UI tree to FTXUI's event loop -----------------------------------
  screen.Loop(ui);
  
  // Terminal should be properly restored by FTXUI at this point
}