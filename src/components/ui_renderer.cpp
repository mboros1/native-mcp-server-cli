#include "ui_renderer.hpp"
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <map>

UIRenderer::UIRenderer(const Config& config, const StateManager& state, const std::vector<Tool>& tools)
  : config_(config), state_(state), tools_(tools) {}

void UIRenderer::SetConnectionStatus(bool connected) {
  is_connected_ = connected;
}

Elements UIRenderer::RenderHelp() const {
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

Elements UIRenderer::RenderToolList() const {
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

Elements UIRenderer::RenderToolInfo(const Tool& tool) const {
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

std::string UIRenderer::GetScreenText() const {
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

Element UIRenderer::RenderConversationLog() const {
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

Element UIRenderer::Render() const {
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