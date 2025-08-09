#include "input_with_history.hpp"

InputWithHistory::InputWithHistory(std::string* content, InputOption options) 
  : input_content_(content), input_options_(options) {
  input_component_ = Input(input_content_, input_options_);
  Add(input_component_);
  LoadHistory();
}

void InputWithHistory::AddToHistory(const std::string& command) {
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

bool InputWithHistory::OnEvent(Event event) {
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

Component InputWithHistory::GetInputComponent() { 
  return input_component_; 
}

void InputWithHistory::NavigateHistoryUp() {
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

void InputWithHistory::NavigateHistoryDown() {
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

void InputWithHistory::LoadHistory() {
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

void InputWithHistory::SaveHistory() {
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

std::string InputWithHistory::EscapeString(const std::string& str) {
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

std::string InputWithHistory::UnescapeString(const std::string& str) {
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