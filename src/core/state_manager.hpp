/**
 * @file state_manager.hpp
 * @brief Central state management for the Native MCP CLI application
 * @author Native MCP Team
 * @date 2025
 * 
 * The StateManager class provides thread-safe centralized state management
 * for all application components including UI state, chat history, connection
 * status, and timeout handling.
 */

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

/**
 * @class StateManager
 * @brief Manages all application state in a thread-safe manner
 * 
 * The StateManager is the central hub for application state, managing:
 * - Application lifecycle (running, exiting)
 * - Display modes for different UI states
 * - Chat history and token counting
 * - Command history
 * - Connection timeouts and retry logic
 * - Configuration persistence
 * 
 * @note Thread-safety is provided through atomic operations and careful state management
 * 
 * @example Basic Usage
 * @code
 * StateManager state;
 * 
 * // Set up display mode
 * state.SetDisplayMode(StateManager::DisplayMode::NORMAL);
 * 
 * // Handle user input
 * state.AddToHistory("user command");
 * state.SetAwaitingResponse("Waiting for server...");
 * 
 * // Write chat messages
 * state.WriteToChatHistory("user", "Hello AI");
 * state.WriteToChatHistory("assistant", "Hello! How can I help?");
 * 
 * // Check timeout state
 * if (state.GetTimeoutState() == StateManager::TimeoutState::SERVER_OVERDUE) {
 *     // Show warning to user
 * }
 * @endcode
 */
class StateManager {
public:
  /**
   * @enum AppState
   * @brief Application lifecycle states
   */
  enum class AppState {
    RUNNING,         ///< Normal operation
    EXIT_REQUESTED,  ///< User requested exit, waiting for confirmation
    EXITING         ///< Application is shutting down
  };

  /**
   * @enum DisplayMode
   * @brief UI display modes for different screens/states
   * 
   * Controls what the UI should display to the user
   */
  enum class DisplayMode {
    NORMAL,              ///< Standard chat interface
    HELP,               ///< Help screen showing available commands
    LIST,               ///< List of available tools/models
    TOOL_INFO,          ///< Detailed information about a specific tool
    UNKNOWN_COMMAND,    ///< Error: unrecognized command
    EXIT_WARNING,       ///< Warning before exit (unsaved changes, etc.)
    EXIT_MESSAGE,       ///< Final message during shutdown
    DUMP_SUCCESS,       ///< Success message after dumping screen
    CHAT_NOT_IMPLEMENTED, ///< Feature not yet implemented
    MCP_ERROR          ///< MCP protocol error occurred
  };

  /**
   * @enum TimeoutState
   * @brief Server response timeout states
   * 
   * Tracks how long we've been waiting for a server response
   * and provides different states for UI feedback
   */
  enum class TimeoutState {
    NORMAL,           ///< Response time within expected bounds (<75% of timeout)
    SERVER_OVERDUE,   ///< Server taking longer than usual (75-100% of timeout)
    RECOMMEND_RESET,  ///< Timeout exceeded, suggest retry (100-120% of timeout)
    AUTO_RESET       ///< Will auto-reset soon (>120% of timeout)
  };

public:
  /**
   * @brief Construct a new StateManager
   * 
   * Initializes all state variables and loads configuration from disk
   */
  StateManager();
  
  // ===== Application State Management =====
  
  /**
   * @brief Get current application state
   * @return Current AppState
   */
  AppState GetAppState() const;
  
  /**
   * @brief Set application state
   * @param state New application state
   * @note Thread-safe through atomic operations
   */
  void SetAppState(AppState state);
  
  /**
   * @brief Request application exit
   * 
   * Sets state to EXIT_REQUESTED. Call ConfirmExit() to proceed
   * or ResetCtrlC() to cancel.
   */
  void RequestExit();
  
  /**
   * @brief Confirm exit request and begin shutdown
   * 
   * Transitions from EXIT_REQUESTED to EXITING
   */
  void ConfirmExit();
  
  /**
   * @brief Check if application is shutting down
   * @return true if state is EXITING
   */
  bool IsExiting() const;
  
  /**
   * @brief Check if exit has been requested
   * @return true if state is EXIT_REQUESTED
   */
  bool IsExitRequested() const;
  
  /**
   * @brief Get current display mode
   * @return Current DisplayMode
   */
  DisplayMode GetDisplayMode() const;
  
  /**
   * @brief Set display mode for UI
   * @param mode New display mode
   */
  void SetDisplayMode(DisplayMode mode);
  
  // ===== Exit and Escape Key Handling =====
  
  /**
   * @brief Handle Ctrl+C press for exit request
   * 
   * Implements double-tap logic: first press requests exit,
   * second press within timeout confirms exit
   */
  void HandleCtrlC();
  
  /**
   * @brief Reset Ctrl+C pending state
   */
  void ResetCtrlC();
  
  /**
   * @brief Check if Ctrl+C exit is pending
   * @return true if waiting for exit confirmation
   */
  bool IsCtrlCPending() const;
  
  /**
   * @brief Handle Escape key press
   * @return true if input should be cleared, false otherwise
   */
  bool HandleEsc();
  
  /**
   * @brief Reset Escape key pending state
   */
  void ResetEsc();
  
  /**
   * @brief Check if Escape action is pending
   * @return true if Escape was recently pressed
   */
  bool IsEscPending() const;
  
  // ===== Tool Selection =====
  
  /**
   * @brief Select a tool/model for use
   * @param tool Pointer to tool to select (can be nullptr to deselect)
   */
  void SelectTool(Tool* tool);
  
  /**
   * @brief Get currently selected tool
   * @return Pointer to selected tool or nullptr if none selected
   */
  Tool* GetSelectedTool() const;
  
  // ===== Command History Management =====
  
  /**
   * @brief Add command to history
   * @param command Command string to add
   * 
   * @example
   * @code
   * state.AddToHistory("/help");
   * state.AddToHistory("Hello AI!");
   * @endcode
   */
  void AddToHistory(const std::string& command);
  
  /**
   * @brief Get command history
   * @return Reference to command history vector
   */
  const std::vector<std::string>& GetHistory() const;
  
  /**
   * @brief Set maximum command history size
   * @param size Maximum number of commands to keep
   */
  void SetMaxHistorySize(int size);
  
  /**
   * @brief Set maximum event log size
   * @param size Maximum number of log entries to keep in memory
   */
  void SetMaxLogSize(int size);
  
  // ===== Conversation Log Management =====
  
  /**
   * @brief Add entry to conversation/event log
   * @param type Type of log entry (USER_MESSAGE, AI_RESPONSE, etc.)
   * @param content Content of the log entry
   */
  void AddLogEntry(LogEntryType type, const std::string& content);
  
  /**
   * @brief Get the event log
   * @return Reference to event log deque
   */
  const std::deque<LogEntry>& GetEventLog() const;
  
  /**
   * @brief Clear all entries from event log
   */
  void ClearEventLog();
  
  // ===== Retry Functionality =====
  
  /**
   * @brief Enable retry option after timeout
   * @param timeout_msg Message describing the timeout
   * @param original_msg Original message that timed out
   * 
   * @example
   * @code
   * state.SetRetryAvailable("Request timed out after 60s", "Tell me about AI");
   * @endcode
   */
  void SetRetryAvailable(const std::string& timeout_msg, const std::string& original_msg);
  
  /**
   * @brief Clear retry availability
   */
  void ClearRetry();
  
  /**
   * @brief Check if retry is available
   * @return true if user can retry last request
   */
  bool IsRetryAvailable() const;
  
  /**
   * @brief Get retry timeout message
   * @return Timeout message or empty string if no retry available
   */
  std::string GetRetryMessage() const;
  
  /**
   * @brief Get original message that can be retried
   * @return Original message or empty string if no retry available
   */
  std::string GetRetryOriginalMessage() const;
  
  // ===== Response Timeout Management =====
  
  /**
   * @brief Mark that we're waiting for a server response
   * @param message The message sent to server
   * 
   * Starts timeout tracking for the request
   */
  void SetAwaitingResponse(const std::string& message);
  
  /**
   * @brief Clear awaiting response state
   * 
   * Called when response is received or request is cancelled
   */
  void ClearAwaitingResponse();
  
  /**
   * @brief Check if waiting for server response
   * @return true if awaiting response
   */
  bool IsAwaitingResponse() const;
  
  /**
   * @brief Get configured server timeout duration
   * @return Timeout duration in milliseconds
   */
  std::chrono::milliseconds GetServerTimeout() const;
  
  /**
   * @brief Get current timeout state
   * @return TimeoutState indicating how long we've been waiting
   */
  TimeoutState GetTimeoutState() const;
  
  /**
   * @brief Get elapsed time since request sent
   * @return Elapsed time in seconds
   */
  std::chrono::seconds GetElapsedTime() const;
  
  // ===== Status and UI Support =====
  
  /**
   * @brief Generate status message for UI display
   * @param has_input_text Whether user has typed something
   * @return Formatted status message string
   * 
   * Generates context-appropriate status messages like:
   * - "Press Ctrl+C again to exit"
   * - "Waiting for response... (45s)"
   * - "Server timeout - press 'r' to retry"
   */
  std::string GetStatusMessage(bool has_input_text = false) const;
  
  // ===== Chat History and Token Management =====
  
  /**
   * @brief Write message to chat history file
   * @param role Message role ("user", "assistant", "system")
   * @param content Message content
   * 
   * Appends to .data/chat-history.json in NDJSON format
   */
  void WriteToChatHistory(const std::string& role, const std::string& content);
  
  /**
   * @brief Get total token count for current conversation
   * @return Total tokens used
   */
  size_t GetTotalTokens() const;
  
  /**
   * @brief Reset token counter to zero
   */
  void ResetTokenCount();
  
  /**
   * @brief Add tokens for assistant response
   * @param tokens Number of tokens to add
   */
  void IncrementTokensForAssistant(size_t tokens);
  
  /**
   * @brief Rotate chat history file (start new conversation)
   * 
   * Moves current chat-history.json to timestamped file
   */
  void RotateChatHistoryFile();
  
  /**
   * @brief Load chat history on application startup
   */
  void LoadChatHistoryOnStartup();
  
  /**
   * @brief Use mocked chat history instead of loading from file
   * @param entries Vector of chat history entries to use
   */
  void UseMockedChatHistory(const std::vector<ChatHistoryEntry>& entries);
  
  /**
   * @brief Get list of available chat history files
   * @return Vector of pairs (filename, first message preview)
   */
  std::vector<std::pair<std::string, std::string>> GetAvailableChatHistories() const;
  
  /**
   * @brief Load a specific chat history file
   * @param index Index in available histories list
   * @return true if successfully loaded
   */
  bool LoadChatHistory(size_t index);
  
  /**
   * @brief Delete a chat history file
   * @param index Index in available histories list
   * @return true if successfully deleted
   */
  bool DeleteChatHistory(size_t index);
  
  /**
   * @brief Format filesystem timestamp for display
   * @param ftime Filesystem time
   * @param is_current Whether this is the current chat
   * @return Formatted timestamp string
   */
  std::string FormatTimestamp(const std::filesystem::file_time_type& ftime, bool is_current = false) const;
  
  /**
   * @brief Get chat history statistics
   * @return Formatted statistics string
   */
  std::string GetChatHistoryStats() const;
  
  /**
   * @brief Compare local history with server history
   * @param server_history Server's chat history array
   * @return Comparison result string
   */
  std::string CompareChatHistoryWithServer(const simdjson::dom::array& server_history) const;
  
  // ===== Configuration Management =====
  
  /**
   * @brief Get mutable configuration object
   * @return Reference to ChatConfig
   */
  ChatConfig& GetConfig();
  
  /**
   * @brief Get read-only configuration object
   * @return Const reference to ChatConfig
   */
  const ChatConfig& GetConfig() const;
  
  /**
   * @brief Load configuration from config.json
   */
  void LoadConfig();
  
  /**
   * @brief Save configuration to config.json
   */
  void SaveConfig();

private:
  /**
   * @brief Extract first user message from chat history file
   * @param filepath Path to chat history file
   * @return First user message or description
   */
  std::string GetFirstUserMessage(const std::filesystem::path& filepath) const;
  
  /**
   * @brief Write log entry to file
   * @param entry Log entry to write
   */
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
  
  // Test mode flag
  bool use_mocked_history_ = false;
};