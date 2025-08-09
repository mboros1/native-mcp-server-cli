#pragma once

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <spdlog/spdlog.h>
#include <simdjson.h>
#include <token_est.h>
#include "../include/types_core.hpp"

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

  enum class TimeoutState {
    NORMAL,      // Within timeout bounds
    SERVER_OVERDUE,     // Approaching timeout (>75% of limit)  
    RECOMMEND_RESET,    // Timeout exceeded, can still wait
    AUTO_RESET   // Auto-reset period, will clear soon
  };

public:
  StateManager();
  
  // Application state
  AppState GetAppState() const;
  void SetAppState(AppState state);
  void RequestExit();
  void ConfirmExit();
  bool IsExiting() const;
  bool IsExitRequested() const;
  DisplayMode GetDisplayMode() const;
  void SetDisplayMode(DisplayMode mode);
  
  // Exit handling
  void HandleCtrlC();
  void ResetCtrlC();
  bool IsCtrlCPending() const;
  bool HandleEsc();  // Returns true if input should be cleared
  void ResetEsc();
  bool IsEscPending() const;
  
  // Tool selection
  void SelectTool(Tool* tool);
  Tool* GetSelectedTool() const;
  
  // History management
  void AddToHistory(const std::string& command);
  const std::vector<std::string>& GetHistory() const;
  void SetMaxHistorySize(int size);
  void SetMaxLogSize(int size);
  
  // Conversation log management
  void AddLogEntry(LogEntryType type, const std::string& content);
  const std::deque<LogEntry>& GetEventLog() const;
  void ClearEventLog();
  
  // Retry functionality
  void SetRetryAvailable(const std::string& timeout_msg, const std::string& original_msg);
  void ClearRetry();
  bool IsRetryAvailable() const;
  std::string GetRetryMessage() const;
  std::string GetRetryOriginalMessage() const;
  
  // Awaiting response management
  void SetAwaitingResponse(const std::string& message);
  void ClearAwaitingResponse();
  bool IsAwaitingResponse() const;
  std::chrono::milliseconds GetServerTimeout() const;
  TimeoutState GetTimeoutState() const;
  std::chrono::seconds GetElapsedTime() const;
  
  // Status message generation
  std::string GetStatusMessage(bool has_input_text = false) const;
  
  // Chat history management
  void WriteToChatHistory(const std::string& role, const std::string& content);
  size_t GetTotalTokens() const;
  void ResetTokenCount();
  void IncrementTokensForAssistant(size_t tokens);
  void RotateChatHistoryFile();
  void LoadChatHistoryOnStartup();
  std::vector<std::pair<std::string, std::string>> GetAvailableChatHistories() const;
  bool LoadChatHistory(size_t index);
  bool DeleteChatHistory(size_t index);
  std::string FormatTimestamp(const std::filesystem::file_time_type& ftime, bool is_current = false) const;
  std::string GetChatHistoryStats() const;
  std::string CompareChatHistoryWithServer(const simdjson::dom::array& server_history) const;
  
  // Configuration management
  ChatConfig& GetConfig();
  const ChatConfig& GetConfig() const;
  void LoadConfig();
  void SaveConfig();

private:
  std::string GetFirstUserMessage(const std::filesystem::path& filepath) const;
  void WriteToLogFile(const LogEntry& entry);

private:
  AppState app_state_ = AppState::RUNNING;
  DisplayMode display_mode_ = DisplayMode::NORMAL;
  
  std::chrono::steady_clock::time_point last_ctrl_c_time_;
  bool ctrl_c_pending_ = false;
  
  std::chrono::steady_clock::time_point last_esc_time_;
  bool esc_pending_ = false;
  
  std::vector<std::string> command_history_;
  std::deque<LogEntry> event_log_;
  Tool* selected_tool_ = nullptr;
  int max_history_size_ = 100;
  int max_log_size_ = 10000;  // Maximum number of log entries to keep in memory
  
  // Retry functionality
  bool retry_available_ = false;
  std::string retry_message_;
  std::string retry_original_message_;
  
  // Awaiting response state for coordinated timeouts
  std::atomic<bool> awaiting_response_{false};
  std::string pending_message_;
  std::chrono::steady_clock::time_point request_start_time_;
  std::chrono::milliseconds server_timeout_{5 * 60 * 1000}; // 5 minutes default
  static constexpr auto CLIENT_BUFFER = std::chrono::seconds(5);
  static constexpr auto AUTO_RESET_DELAY = std::chrono::minutes(2);
  
  // Token tracking
  std::atomic<size_t> total_context_tokens_{0};
  token_est::TokenEstimator tokenizer_;
  
  // Configuration
  ChatConfig config_;
};