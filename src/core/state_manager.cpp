#include "state_manager.hpp"
#include <json_struct.h>
#include <simdjson.h>
#include <iomanip>
#include <ctime>

using namespace simdjson;

StateManager::StateManager() : tokenizer_() {
  // Initialize with defaults
}

// Application state
StateManager::AppState StateManager::GetAppState() const {
  return app_state_;
}

void StateManager::SetAppState(AppState state) {
  app_state_ = state;
}

void StateManager::RequestExit() {
  if (app_state_ == AppState::RUNNING) {
    app_state_ = AppState::EXIT_REQUESTED;
    display_mode_ = DisplayMode::EXIT_MESSAGE;
  }
}

void StateManager::ConfirmExit() {
  app_state_ = AppState::EXITING;
}

bool StateManager::IsExiting() const {
  return app_state_ == AppState::EXITING;
}

bool StateManager::IsExitRequested() const {
  return app_state_ == AppState::EXIT_REQUESTED;
}

StateManager::DisplayMode StateManager::GetDisplayMode() const {
  return display_mode_;
}

void StateManager::SetDisplayMode(DisplayMode mode) {
  SPDLOG_DEBUG("Setting display mode from {} to {}", static_cast<int>(display_mode_), static_cast<int>(mode));
  display_mode_ = mode;
}

// Exit handling
void StateManager::HandleCtrlC() {
  auto now = std::chrono::steady_clock::now();
  
  if (ctrl_c_pending_) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ctrl_c_time_);
    SPDLOG_DEBUG("Second Ctrl+C received, elapsed: {}ms", elapsed.count());
    if (elapsed.count() < 2000) {
      SPDLOG_INFO("Double Ctrl+C detected - requesting exit");
      if (app_state_ == AppState::RUNNING) {
        app_state_ = AppState::EXIT_REQUESTED;
        display_mode_ = DisplayMode::EXIT_MESSAGE;
      }
      return;
    }
  }
  
  SPDLOG_DEBUG("First Ctrl+C received");
  ctrl_c_pending_ = true;
  last_ctrl_c_time_ = now;
  display_mode_ = DisplayMode::EXIT_WARNING;
}

void StateManager::ResetCtrlC() {
  ctrl_c_pending_ = false;
}

bool StateManager::IsCtrlCPending() const {
  if (!ctrl_c_pending_) return false;
  
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ctrl_c_time_);
  return elapsed.count() < 2000;
}

bool StateManager::HandleEsc() {
  auto now = std::chrono::steady_clock::now();
  
  if (esc_pending_) {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_esc_time_);
    SPDLOG_DEBUG("Second Esc received, elapsed: {}ms", elapsed.count());
    if (elapsed.count() < 2000) {
      SPDLOG_INFO("Double Esc detected - should clear input");
      esc_pending_ = false;
      return true;  // Return true to indicate input should be cleared
    }
  }
  
  SPDLOG_DEBUG("First Esc received");
  esc_pending_ = true;
  last_esc_time_ = now;
  return false;  // First Esc, don't clear yet
}

void StateManager::ResetEsc() {
  esc_pending_ = false;
}

bool StateManager::IsEscPending() const {
  if (!esc_pending_) return false;
  
  auto now = std::chrono::steady_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_esc_time_);
  return elapsed.count() < 2000;
}

// Tool selection
void StateManager::SelectTool(Tool* tool) {
  selected_tool_ = tool;
  display_mode_ = tool ? DisplayMode::TOOL_INFO : DisplayMode::UNKNOWN_COMMAND;
}

Tool* StateManager::GetSelectedTool() const {
  return selected_tool_;
}

// History management
void StateManager::AddToHistory(const std::string& command) {
  command_history_.push_back(command);
  if (command_history_.size() > static_cast<size_t>(max_history_size_)) {
    command_history_.erase(command_history_.begin());
  }
}

const std::vector<std::string>& StateManager::GetHistory() const {
  return command_history_;
}

void StateManager::SetMaxHistorySize(int size) {
  max_history_size_ = size;
}

void StateManager::SetMaxLogSize(int size) {
  max_log_size_ = size;
}

// Conversation log management
void StateManager::AddLogEntry(LogEntryType type, const std::string& content) {
  auto entry = LogEntry{
    type, 
    content, 
    std::chrono::system_clock::now()
  };
  event_log_.push_back(entry);
  
  // Enforce size limit - remove oldest entries if needed
  while (event_log_.size() > static_cast<size_t>(max_log_size_)) {
    event_log_.pop_front();
  }
  
  // Write to persistent log file
  WriteToLogFile(entry);
}

const std::deque<LogEntry>& StateManager::GetEventLog() const {
  return event_log_;
}

void StateManager::ClearEventLog() {
  event_log_.clear();
}

// Retry functionality
void StateManager::SetRetryAvailable(const std::string& timeout_msg, const std::string& original_msg) {
  retry_available_ = true;
  retry_message_ = timeout_msg;
  retry_original_message_ = original_msg;
}

void StateManager::ClearRetry() {
  retry_available_ = false;
  retry_message_.clear();
  retry_original_message_.clear();
}

bool StateManager::IsRetryAvailable() const {
  return retry_available_;
}

std::string StateManager::GetRetryMessage() const {
  return retry_message_;
}

std::string StateManager::GetRetryOriginalMessage() const {
  return retry_original_message_;
}

// Awaiting response management
void StateManager::SetAwaitingResponse(const std::string& message) {
  awaiting_response_ = true;
  pending_message_ = message;
  request_start_time_ = std::chrono::steady_clock::now();
}

void StateManager::ClearAwaitingResponse() {
  awaiting_response_ = false;
  pending_message_.clear();
}

bool StateManager::IsAwaitingResponse() const {
  return awaiting_response_.load();
}

std::chrono::milliseconds StateManager::GetServerTimeout() const {
  return server_timeout_;
}

StateManager::TimeoutState StateManager::GetTimeoutState() const {
  if (!awaiting_response_) return TimeoutState::NORMAL;
  
  auto elapsed = std::chrono::steady_clock::now() - request_start_time_;
  
  if (elapsed > server_timeout_ + CLIENT_BUFFER + AUTO_RESET_DELAY) {
    return TimeoutState::AUTO_RESET;
  } else if (elapsed > server_timeout_ + CLIENT_BUFFER) {
    return TimeoutState::RECOMMEND_RESET;
  } else if (elapsed > server_timeout_) {
    return TimeoutState::SERVER_OVERDUE;
  }
  
  return TimeoutState::NORMAL;
}

std::chrono::seconds StateManager::GetElapsedTime() const {
  if (!awaiting_response_) return std::chrono::seconds(0);
  auto elapsed = std::chrono::steady_clock::now() - request_start_time_;
  return std::chrono::duration_cast<std::chrono::seconds>(elapsed);
}

// Status message generation
std::string StateManager::GetStatusMessage(bool has_input_text) const {
  if (IsCtrlCPending()) {
    return "Press Ctrl+C again to exit";
  }
  if (IsEscPending()) {
    return "Press Esc again to clear the input";
  }
  if (retry_available_) {
    return "Click 'Retry' to resend the last message, or continue typing normally";
  }
  if (IsAwaitingResponse()) {
    auto timeout_state = GetTimeoutState();
    switch (timeout_state) {
      case TimeoutState::SERVER_OVERDUE:
        return "Server overdue - press <Esc> or type /reset to cancel";
      case TimeoutState::RECOMMEND_RESET:
        return "Server not responding - press <Esc> or /reset to interrupt";
      default:
        return "Awaiting response... (press <Esc> to cancel)";
    }
  }
  if (has_input_text) {
    return "Hit Ctrl+N or click 'Send' to submit";
  }
  return "Type /help for commands • Ctrl+C twice to exit • Use ↑/↓ for history";
}

// Chat history management
void StateManager::WriteToChatHistory(const std::string& role, const std::string& content) {
  // Note: The check for IsAwaitingResponse() has been removed.
  // Blocking duplicate messages should happen at the SendChatMessage level,
  // not here. This function should always write when called.
  
  // Skip writing to file if using mocked history
  if (use_mocked_history_) {
    SPDLOG_DEBUG("Using mocked history, skipping file write");
    return;
  }
  
  // Calculate token count
  size_t tokens = tokenizer_.count_tokens(content);
  
  // Add to running total
  total_context_tokens_.fetch_add(tokens);
  
  // Create chat history entry
  auto entry = ChatHistoryEntry{
    role,
    content,
    tokens,
    std::chrono::system_clock::now()
  };
  
  // Create .data directory if it doesn't exist
  std::filesystem::create_directories(".data");
  
  // Open chat-history.json in append mode
  std::ofstream chat_file(".data/chat-history.json", std::ios::app);
  if (!chat_file.is_open()) {
    SPDLOG_ERROR("Failed to open chat history file");
    return;
  }
  
  // Serialize to compact JSON using json_struct (NDJSON format)
  std::string json = JS::serializeStruct(entry, JS::SerializerOptions(JS::SerializerOptions::Compact));
  
  // Write as a single line (NDJSON format)
  chat_file << json << "\n";
  
  chat_file.close();
  
  SPDLOG_DEBUG("Wrote {} tokens to chat history: {}", tokens, role);
}

size_t StateManager::GetTotalTokens() const {
  return total_context_tokens_.load();
}

void StateManager::ResetTokenCount() {
  total_context_tokens_.store(0);
}

void StateManager::IncrementTokensForAssistant(size_t tokens) {
  total_context_tokens_.fetch_add(tokens);
}

void StateManager::RotateChatHistoryFile() {
  const std::string chat_history_file = ".data/chat-history.json";
  
  // Check if file exists
  if (!std::filesystem::exists(chat_history_file)) {
    SPDLOG_INFO("No chat history file to rotate");
    return;
  }
  
  try {
    // Create timestamp string (ISO 8601 format with safe filename characters)
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      now.time_since_epoch()) % 1000;
    
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H-%M-%S", std::gmtime(&time_t));
    
    // Create backup filename
    std::string backup_file = ".data/chat-history-" + std::string(timestamp) + 
                             "-" + std::to_string(ms.count()) + "Z.json";
    
    // Rename current file to backup
    std::filesystem::rename(chat_history_file, backup_file);
    
    SPDLOG_INFO("Rotated chat history to: {}", backup_file);
    
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Error rotating chat history file: {}", e.what());
  }
}

void StateManager::UseMockedChatHistory(const std::vector<ChatHistoryEntry>& entries) {
  use_mocked_history_ = true;
  
  // Clear existing history
  event_log_.clear();
  total_context_tokens_ = 0;
  
  // Load the mocked entries
  for (const auto& entry : entries) {
    // Convert role to LogEntryType
    LogEntryType type = LogEntryType::USER;
    if (entry.role == "assistant") {
      type = LogEntryType::RESPONSE;  // Use RESPONSE instead of ASSISTANT
    } else if (entry.role == "system") {
      type = LogEntryType::SYSTEM;
    }
    
    // Add to event log
    LogEntry log_entry;
    log_entry.type = type;
    log_entry.content = entry.content;
    log_entry.timestamp = std::chrono::system_clock::now();  // Use system_clock instead of steady_clock
    event_log_.push_back(log_entry);  // Directly push to event_log_
    
    // Update token count
    total_context_tokens_ += entry.token_cnt;
  }
  
  SPDLOG_INFO("Loaded {} mocked chat history entries with {} total tokens", 
              entries.size(), total_context_tokens_.load());
}

void StateManager::LoadChatHistoryOnStartup() {
  // Skip loading from file if using mocked history
  if (use_mocked_history_) {
    SPDLOG_INFO("Using mocked chat history, skipping file load");
    return;
  }
  
  const std::string chat_history_file = ".data/chat-history.json";
  
  // Check if file exists
  if (!std::filesystem::exists(chat_history_file)) {
    SPDLOG_INFO("No existing chat history file found");
    return;
  }
  
  try {
    // Read file
    std::ifstream file(chat_history_file);
    if (!file.is_open()) {
      SPDLOG_ERROR("Failed to open chat history file");
      return;
    }
    
    std::string line;
    size_t total_tokens = 0;
    int loaded_count = 0;
    
    // Parse each line (NDJSON format)
    while (std::getline(file, line)) {
      if (line.empty()) continue;
      
      try {
        // Parse JSON using simdjson
        dom::parser parser;
        dom::element doc;
        auto error = parser.parse(line).get(doc);
        
        if (error) {
          SPDLOG_WARN("Failed to parse chat history line: {}", error_message(error));
          continue;
        }
        
        // Extract fields
        if (!doc["role"].is_string() || !doc["content"].is_string() || !doc["token_cnt"].is_number()) {
          SPDLOG_WARN("Invalid chat history entry format");
          continue;
        }
        
        std::string role = std::string(doc["role"].get_string().value());
        std::string content = std::string(doc["content"].get_string().value());
        size_t token_cnt = doc["token_cnt"].get_uint64();
        
        // Add to conversation log (convert role to LogEntryType)
        LogEntryType log_type = (role == "user") ? LogEntryType::USER : LogEntryType::RESPONSE;
        
        // Create log entry with original timestamp if available
        std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
        if (doc["timestamp"].is_string()) {
          // Parse ISO 8601 timestamp - simplified parsing
          std::string time_str = std::string(doc["timestamp"].get_string().value());
          std::tm tm = {};
          std::istringstream ss(time_str);
          ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
          if (!ss.fail()) {
            timestamp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
          }
        }
        
        auto entry = LogEntry{log_type, content, timestamp};
        event_log_.push_back(entry);
        
        // Add to running token count
        total_tokens += token_cnt;
        loaded_count++;
        
      } catch (const std::exception& e) {
        SPDLOG_WARN("Error parsing chat history entry: {}", e.what());
        continue;
      }
    }
    
    // Update total token count
    total_context_tokens_.store(total_tokens);
    
    SPDLOG_INFO("Loaded {} chat history entries with {} total tokens", loaded_count, total_tokens);
    
    // Check if last message is from user (incomplete conversation)
    // Look through the event_log deque from the end to find the last user message
    std::string last_user_message;
    bool found_incomplete = false;
    
    for (auto it = event_log_.rbegin(); it != event_log_.rend(); ++it) {
      if (it->type == LogEntryType::USER) {
        last_user_message = it->content;
        found_incomplete = true;
        break;
      } else if (it->type == LogEntryType::RESPONSE) {
        // Found a response before a user message, conversation is complete
        break;
      }
    }
    
    if (found_incomplete && !last_user_message.empty()) {
      SPDLOG_INFO("Detected incomplete conversation - last user message: {}", last_user_message.substr(0, 50));
      SetRetryAvailable("Incomplete conversation detected - last message was not responded to", last_user_message);
    }
    
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Error loading chat history: {}", e.what());
  }
}

std::vector<std::pair<std::string, std::string>> StateManager::GetAvailableChatHistories() const {
  std::vector<std::pair<std::string, std::string>> histories;
  const std::string data_dir = ".data";
  const std::string current_file = ".data/chat-history.json";
  
  try {
    // First add the current chat-history.json if it exists
    if (std::filesystem::exists(current_file)) {
      std::string first_user_msg = GetFirstUserMessage(current_file);
      histories.push_back({current_file, first_user_msg});
    }
    
    // Find all archived chat-history-*.json files (excluding .del files)
    for (const auto& entry : std::filesystem::directory_iterator(data_dir)) {
      if (entry.is_regular_file()) {
        std::string filename = entry.path().filename().string();
        std::string filepath = entry.path().string();
        
        // Skip current file (already added), deleted files, and non-chat files
        if (filepath == current_file) continue;
        if (filename.ends_with(".del")) continue;
        
        if (filename.starts_with("chat-history-") && filename.ends_with(".json")) {
          // Try to read first user message from file
          std::string first_user_msg = GetFirstUserMessage(entry.path());
          histories.push_back({filepath, first_user_msg});
        }
      }
    }
    
    // Sort by modification time (newest first), but keep current at top
    if (histories.size() > 1) {
      std::sort(histories.begin() + 1, histories.end(),
        [](const auto& a, const auto& b) {
          return std::filesystem::last_write_time(a.first) > 
                 std::filesystem::last_write_time(b.first);
        });
    }
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Error listing chat histories: {}", e.what());
  }
  
  return histories;
}

std::string StateManager::GetFirstUserMessage(const std::filesystem::path& filepath) const {
  try {
    std::ifstream file(filepath);
    if (!file.is_open()) return "(unable to read)";
    
    std::string line;
    while (std::getline(file, line)) {
      // Parse JSON line to find first user message
      JS::ParseContext context(line);
      ChatHistoryEntry entry;
      if (context.parseTo(entry) == JS::Error::NoError) {
        if (entry.role == "user") {
          // Return first 40 chars of message (or less if shorter)
          if (entry.content.length() <= 40) {
            return entry.content;
          } else {
            return entry.content.substr(0, 40) + "...";
          }
        }
      }
    }
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Error reading {}: {}", filepath.string(), e.what());
  }
  return "(no user message found)";
}

bool StateManager::LoadChatHistory(size_t index) {
  auto histories = GetAvailableChatHistories();
  
  if (index == 0 || index > histories.size()) {
    SPDLOG_ERROR("Invalid chat history index: {}", index);
    return false;
  }
  
  const auto& selected_path = histories[index - 1].first;
  const std::string current_file = ".data/chat-history.json";
  
  // If selecting the current file, nothing to do
  if (selected_path == current_file) {
    SPDLOG_INFO("Already viewing current conversation");
    return true;
  }
  
  try {
    // First rotate current file if it exists
    if (std::filesystem::exists(current_file)) {
      RotateChatHistoryFile();
    }
    
    // Now copy the selected file to be the current one
    std::filesystem::copy_file(selected_path, current_file, 
                                std::filesystem::copy_options::overwrite_existing);
    
    // Delete the old archived version
    std::filesystem::remove(selected_path);
    
    SPDLOG_INFO("Loaded chat history from {}", selected_path);
    return true;
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Failed to load chat history: {}", e.what());
    return false;
  }
}

bool StateManager::DeleteChatHistory(size_t index) {
  auto histories = GetAvailableChatHistories();
  
  if (index == 0 || index > histories.size()) {
    SPDLOG_ERROR("Invalid chat history index: {}", index);
    return false;
  }
  
  const auto& selected_path = histories[index - 1].first;
  const std::string current_file = ".data/chat-history.json";
  
  // Don't allow deleting current conversation
  if (selected_path == current_file) {
    SPDLOG_WARN("Cannot delete current conversation. Use /new to start fresh.");
    return false;
  }
  
  try {
    // Soft delete by appending .del
    std::string deleted_path = selected_path + ".del";
    std::filesystem::rename(selected_path, deleted_path);
    
    SPDLOG_INFO("Soft-deleted chat history: {} -> {}", 
                selected_path, deleted_path);
    return true;
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Failed to delete chat history: {}", e.what());
    return false;
  }
}

std::string StateManager::FormatTimestamp(const std::filesystem::file_time_type& ftime, bool is_current) const {
  if (is_current) {
    return "Current";
  }
  
  // Convert file_time_type to system_clock time_point
  auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
    ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now()
  );
  
  // Format the full date/time
  auto time_t = std::chrono::system_clock::to_time_t(sctp);
  std::tm tm = *std::localtime(&time_t);
  std::ostringstream date_oss;
  date_oss << std::put_time(&tm, "%b %d, %Y %I:%M %p");
  std::string date_str = date_oss.str();
  
  // Calculate relative time
  auto now = std::chrono::system_clock::now();
  auto diff = now - sctp;
  auto hours = std::chrono::duration_cast<std::chrono::hours>(diff).count();
  auto days = hours / 24;
  
  std::string relative_str;
  if (days > 0) {
    relative_str = std::to_string(days) + " day" + (days == 1 ? "" : "s") + " ago";
  } else if (hours > 0) {
    relative_str = std::to_string(hours) + " hour" + (hours == 1 ? "" : "s") + " ago";
  } else {
    auto mins = std::chrono::duration_cast<std::chrono::minutes>(diff).count();
    if (mins > 0) {
      relative_str = std::to_string(mins) + " minute" + (mins == 1 ? "" : "s") + " ago";
    } else {
      relative_str = "just now";
    }
  }
  
  return date_str + " (" + relative_str + ")";
}

std::string StateManager::GetChatHistoryStats() const {
  const std::string chat_history_file = ".data/chat-history.json";
  
  if (!std::filesystem::exists(chat_history_file)) {
    return "File: 0 entries, 0 tokens";
  }
  
  try {
    std::ifstream file(chat_history_file);
    if (!file.is_open()) {
      return "File: Error reading file";
    }
    
    std::string line;
    int file_entries = 0;
    size_t file_tokens = 0;
    
    while (std::getline(file, line)) {
      if (line.empty()) continue;
      
      try {
        dom::parser parser;
        dom::element doc;
        auto error = parser.parse(line).get(doc);
        
        if (error) continue;
        
        if (doc["role"].is_string() && doc["content"].is_string() && doc["token_cnt"].is_number()) {
          file_entries++;
          file_tokens += doc["token_cnt"].get_uint64();
        }
      } catch (...) {
        continue;
      }
    }
    
    return "File: " + std::to_string(file_entries) + " entries, " + std::to_string(file_tokens) + " tokens";
    
  } catch (const std::exception& e) {
    return "File: Error - " + std::string(e.what());
  }
}

std::string StateManager::CompareChatHistoryWithServer(const dom::array& server_history) const {
  const std::string chat_history_file = ".data/chat-history.json";
  
  if (!std::filesystem::exists(chat_history_file)) {
    return "⚠ File missing - cannot compare";
  }
  
  std::vector<ChatHistoryEntry> file_entries;
  int content_mismatches = 0;
  int token_mismatches = 0;
  int role_mismatches = 0;
  
  try {
    // Read file entries
    std::ifstream file(chat_history_file);
    if (!file.is_open()) {
      return "⚠ Cannot open file for comparison";
    }
    
    std::string line;
    while (std::getline(file, line)) {
      if (line.empty()) continue;
      
      try {
        dom::parser parser;
        dom::element doc;
        auto error = parser.parse(line).get(doc);
        
        if (!error && doc["role"].is_string() && doc["content"].is_string() && doc["token_cnt"].is_number()) {
          ChatHistoryEntry entry;
          entry.role = std::string(doc["role"].get_string().value());
          entry.content = std::string(doc["content"].get_string().value());
          entry.token_cnt = doc["token_cnt"].get_uint64();
          if (doc["timestamp"].is_string()) {
            // Parse ISO 8601 timestamp string to time_point
            std::string timestamp_str = std::string(doc["timestamp"].get_string().value());
            // For now, use current time - could parse ISO string if needed
            entry.timestamp = std::chrono::system_clock::now();
          }
          file_entries.push_back(entry);
        }
      } catch (const std::exception& e) {
        SPDLOG_DEBUG("Error parsing file entry: {}", e.what());
      }
    }
    
    // Compare with server entries
    size_t min_size = std::min(file_entries.size(), server_history.size());
    
    for (size_t i = 0; i < min_size; i++) {
      auto server_entry = server_history.at(i);
      if (!server_entry.is_object()) continue;
      
      auto& file_entry = file_entries[i];
      
      // Compare role
      if (server_entry["role"].is_string()) {
        std::string server_role = std::string(server_entry["role"].get_string().value());
        if (server_role != file_entry.role) {
          role_mismatches++;
          SPDLOG_DEBUG("Role mismatch at index {}: file='{}' server='{}'", i, file_entry.role, server_role);
        }
      }
      
      // Compare content
      if (server_entry["content"].is_string()) {
        std::string server_content = std::string(server_entry["content"].get_string().value());
        if (server_content != file_entry.content) {
          content_mismatches++;
          SPDLOG_DEBUG("Content mismatch at index {}", i);
        }
      }
      
      // Verify token count by recalculating
      if (server_entry["content"].is_string()) {
        std::string server_content = std::string(server_entry["content"].get_string().value());
        size_t calculated_tokens = tokenizer_.count_tokens(server_content);
        
        if (calculated_tokens != file_entry.token_cnt) {
          token_mismatches++;
          SPDLOG_DEBUG("Token count mismatch at index {}: stored={} calculated={}", i, file_entry.token_cnt, calculated_tokens);
        }
      }
    }
    
    // Build comprehensive report
    std::ostringstream report;
    report << "Chat History Sync Analysis:\n";
    report << "File entries: " << file_entries.size() << ", Server entries: " << server_history.size() << "\n";
    
    if (file_entries.size() != server_history.size()) {
      report << "⚠ Count mismatch: " << abs((int)file_entries.size() - (int)server_history.size()) << " entry difference\n";
    }
    
    if (content_mismatches > 0) {
      report << "⚠ " << content_mismatches << " message content(s) out of sync\n";
    }
    
    if (role_mismatches > 0) {
      report << "⚠ " << role_mismatches << " role(s) out of sync\n";
    }
    
    if (token_mismatches > 0) {
      report << "⚠ " << token_mismatches << " token count(s) incorrect\n";
    }
    
    if (file_entries.size() == server_history.size() && 
        content_mismatches == 0 && role_mismatches == 0 && token_mismatches == 0) {
      report << "✓ Perfect sync - all entries match";
    }
    
    return report.str();
    
  } catch (const std::exception& e) {
    return "⚠ Comparison failed: " + std::string(e.what());
  }
}

// Configuration management
ChatConfig& StateManager::GetConfig() {
  return config_;
}

const ChatConfig& StateManager::GetConfig() const {
  return config_;
}

void StateManager::LoadConfig() {
  std::filesystem::path config_path = std::filesystem::path(".config") / "chat-config.json";
  
  if (std::filesystem::exists(config_path)) {
    try {
      std::ifstream file(config_path);
      if (file.is_open()) {
        std::string json_str((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
        
        JS::ParseContext context(json_str);
        auto error = context.parseTo(config_);
        
        if (error == JS::Error::NoError) {
          SPDLOG_INFO("Loaded configuration from {}: model={}, effort={}", 
                      config_path.string(), config_.model, config_.reasoning_effort);
        } else {
          SPDLOG_ERROR("Failed to parse config file");
        }
      }
    } catch (const std::exception& e) {
      SPDLOG_ERROR("Failed to load config: {}", e.what());
    }
  } else {
    SPDLOG_INFO("No config file found at {}, using defaults", config_path.string());
    // Create .config directory if it doesn't exist
    std::filesystem::create_directories(config_path.parent_path());
    SaveConfig();  // Save default config
  }
}

void StateManager::SaveConfig() {
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

// Private methods
void StateManager::WriteToLogFile(const LogEntry& entry) {
  // Create .data directory if it doesn't exist
  std::filesystem::create_directories(".data");
  
  // Open log file in append mode
  std::ofstream log_file(".data/event.log", std::ios::app);
  if (!log_file.is_open()) {
    SPDLOG_ERROR("Failed to open event log file");
    return;
  }
  
  // Serialize to compact JSON using json_struct
  std::string json = JS::serializeStruct(entry, JS::SerializerOptions(JS::SerializerOptions::Compact));
  
  // Write as a single line (NDJSON format)
  log_file << json << "\n";
  
  log_file.close();
}