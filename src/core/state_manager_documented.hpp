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
 * @note All public methods are thread-safe through internal mutex protection
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
   * @note Thread-safe
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
   * @return true if state is EXIT_REQUESTED or EXITING
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
   * 
   * @example Showing help screen
   * @code
   * state.SetDisplayMode(StateManager::DisplayMode::HELP);
   * // UI will now render help content
   * @endcode
   */
  void SetDisplayMode(DisplayMode mode);
  
  // ===== Exit Handling =====
  
  /**
   * @brief Handle Ctrl+C press
   * 
   * Implements triple-Ctrl+C exit pattern:
   * - 1st press: Clear current input
   * - 2nd press (within 2s): Show exit warning
   * - 3rd press (within 2s): Request exit
   * 
   * @note Resets automatically after 2 seconds of inactivity
   */
  void HandleCtrlC();
  
  /**
   * @brief Reset Ctrl+C counter
   * 
   * Clears the Ctrl+C press count, canceling any pending exit
   */
  void ResetCtrlC();
  
  /**
   * @brief Check if Ctrl+C was pressed recently
   * @return true if Ctrl+C count > 0
   */
  bool IsCtrlCPending() const;
  
  /**
   * @brief Handle Escape key press
   * @return true if input should be cleared
   * 
   * Similar to Ctrl+C but with different thresholds
   */
  bool HandleEsc();
  
  /**
   * @brief Reset Escape counter
   */
  void ResetEsc();
  
  /**
   * @brief Check if Escape was pressed recently
   * @return true if Escape count > 0
   */
  bool IsEscPending() const;
  
  // ===== Tool Selection =====
  
  /**
   * @brief Select a tool for detailed view
   * @param tool Pointer to tool to select (can be nullptr to deselect)
   */
  void SelectTool(Tool* tool);
  
  /**
   * @brief Get currently selected tool
   * @return Pointer to selected tool or nullptr
   */
  Tool* GetSelectedTool() const;
  
  // ===== History Management =====
  
  /**
   * @brief Add command to history
   * @param command Command string to add
   * 
   * Maintains a bounded history buffer, removing oldest
   * entries when max size is reached
   * 
   * @example
   * @code
   * state.AddToHistory("/help");
   * state.AddToHistory("/model gpt-4");
   * auto history = state.GetHistory();
   * // history contains both commands
   * @endcode
   */
  void AddToHistory(const std::string& command);
  
  /**
   * @brief Get command history
   * @return Vector of historical commands (newest last)
   */
  const std::vector<std::string>& GetHistory() const;
  
  /**
   * @brief Set maximum history size
   * @param size Maximum number of commands to keep
   */
  void SetMaxHistorySize(int size);
  
  /**
   * @brief Set maximum log size
   * @param size Maximum number of log entries to keep
   */
  void SetMaxLogSize(int size);
  
  // ===== Event Log Management =====
  
  /**
   * @brief Add entry to event log
   * @param type Type of log entry (USER, SYSTEM, RESPONSE, ERROR, BLOCKED)
   * @param content Log message content
   * 
   * @example Logging different event types
   * @code
   * state.AddLogEntry(LogEntryType::USER, "User typed: hello");
   * state.AddLogEntry(LogEntryType::SYSTEM, "Connecting to server...");
   * state.AddLogEntry(LogEntryType::RESPONSE, "Server replied: Hi!");
   * state.AddLogEntry(LogEntryType::ERROR, "Connection failed");
   * @endcode
   */
  void AddLogEntry(LogEntryType type, const std::string& content);
  
  /**
   * @brief Get event log
   * @return Deque of log entries (oldest first)
   */
  const std::deque<LogEntry>& GetEventLog() const;
  
  /**
   * @brief Clear all event log entries
   */
  void ClearEventLog();
  
  // ===== Retry Functionality =====
  
  /**
   * @brief Enable retry for failed request
   * @param timeout_msg Message about the timeout
   * @param original_msg Original message that can be retried
   * 
   * @example
   * @code
   * state.SetRetryAvailable(
   *     "Server timeout after 30s",
   *     "What is the weather today?"
   * );
   * // User can now press 'r' to retry
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
   * @brief Get retry status message
   * @return Message explaining why retry is available
   */
  std::string GetRetryMessage() const;
  
  /**
   * @brief Get original message for retry
   * @return The message that can be retried
   */
  std::string GetRetryOriginalMessage() const;
  
  // ===== Response Awaiting Management =====
  
  /**
   * @brief Set awaiting response state
   * @param message Message being sent to server
   * 
   * Starts timeout tracking and prevents duplicate sends
   * 
   * @example
   * @code
   * state.SetAwaitingResponse("Calculating prime numbers...");
   * // UI shows waiting indicator
   * // Timeout tracking begins
   * @endcode
   */
  void SetAwaitingResponse(const std::string& message);
  
  /**
   * @brief Clear awaiting response state
   * 
   * Call when response received or request fails
   */
  void ClearAwaitingResponse();
  
  /**
   * @brief Check if awaiting server response
   * @return true if waiting for response
   */
  bool IsAwaitingResponse() const;
  
  /**
   * @brief Get configured server timeout duration
   * @return Timeout duration in milliseconds
   */
  std::chrono::milliseconds GetServerTimeout() const;
  
  /**
   * @brief Get current timeout state
   * @return TimeoutState based on elapsed time
   * 
   * @example Handling timeout states
   * @code
   * switch(state.GetTimeoutState()) {
   *     case TimeoutState::NORMAL:
   *         // Show normal waiting indicator
   *         break;
   *     case TimeoutState::SERVER_OVERDUE:
   *         // Show warning that server is slow
   *         break;
   *     case TimeoutState::RECOMMEND_RESET:
   *         // Suggest user retry or cancel
   *         break;
   *     case TimeoutState::AUTO_RESET:
   *         // Will auto-cancel soon
   *         break;
   * }
   * @endcode
   */
  TimeoutState GetTimeoutState() const;
  
  /**
   * @brief Get elapsed time since request sent
   * @return Elapsed time in seconds
   */
  std::chrono::seconds GetElapsedTime() const;
  
  // ===== Status Message Generation =====
  
  /**
   * @brief Generate status message for UI
   * @param has_input_text Whether input field has text
   * @return Formatted status message
   * 
   * Generates contextual status messages based on current state:
   * - Connection status
   * - Timeout warnings
   * - Exit confirmations
   * - Input hints
   */
  std::string GetStatusMessage(bool has_input_text = false) const;
  
  // ===== Chat History Management =====
  
  /**
   * @brief Write message to chat history file
   * @param role Message role ("user", "assistant", "system")
   * @param content Message content
   * 
   * Writes to NDJSON file and updates token count.
   * Blocks duplicate user messages when awaiting response.
   * 
   * @example
   * @code
   * state.WriteToChatHistory("user", "What is 2+2?");
   * state.WriteToChatHistory("assistant", "2+2 equals 4");
   * std::cout << "Total tokens: " << state.GetTotalTokens() << "\n";
   * @endcode
   */
  void WriteToChatHistory(const std::string& role, const std::string& content);
  
  /**
   * @brief Get total token count for conversation
   * @return Estimated total tokens
   */
  size_t GetTotalTokens() const;
  
  /**
   * @brief Reset token counter to zero
   */
  void ResetTokenCount();
  
  /**
   * @brief Increment token count for assistant message
   * @param tokens Number of tokens to add
   */
  void IncrementTokensForAssistant(size_t tokens);
  
  /**
   * @brief Rotate chat history to new file
   * 
   * Creates new timestamped file for next conversation
   */
  void RotateChatHistoryFile();
  
  /**
   * @brief Load most recent chat history on startup
   */
  void LoadChatHistoryOnStartup();
  
  /**
   * @brief Get list of chat history files
   * @return Vector of ChatHistoryFile structs
   */
  std::vector<ChatHistoryFile> GetChatHistoryFiles() const;
  
  /**
   * @brief Load specific chat history file
   * @param index Index in file list
   * @return true if successfully loaded
   */
  bool LoadChatHistory(size_t index);
  
  /**
   * @brief Delete chat history file
   * @param index Index in file list
   * @return true if successfully deleted
   */
  bool DeleteChatHistory(size_t index);
  
  /**
   * @brief Get statistics about chat history
   * @return Formatted statistics string
   */
  std::string GetChatHistoryStats() const;
  
  /**
   * @brief Compare local and server chat histories
   * @param server_messages Messages from server
   * @return Comparison result with differences
   */
  ChatComparison CompareChatHistoryWithServer(
      const std::vector<ChatMessage>& server_messages) const;
  
  // ===== Configuration Management =====
  
  /**
   * @brief Get mutable configuration object
   * @return Reference to ChatConfig
   * 
   * @example Modifying configuration
   * @code
   * auto& config = state.GetConfig();
   * config.model = "gpt-4";
   * config.server_timeout_ms = 60000;
   * state.SaveConfig();
   * @endcode
   */
  ChatConfig& GetConfig();
  
  /**
   * @brief Get configuration object (const)
   * @return Const reference to ChatConfig
   */
  const ChatConfig& GetConfig() const;
  
  /**
   * @brief Load configuration from disk
   * 
   * Loads from .config/chat-config.json
   */
  void LoadConfig();
  
  /**
   * @brief Save configuration to disk
   * 
   * Saves to .config/chat-config.json
   */
  void SaveConfig();
  
private:
  // Private methods documented in implementation file
  void WriteToLogFile(const LogEntry& entry);
  
private:
  // Member variables (implementation details)
  mutable std::mutex state_mutex_;
  
  AppState app_state_ = AppState::RUNNING;
  DisplayMode display_mode_ = DisplayMode::NORMAL;
  Tool* selected_tool_ = nullptr;
  
  std::vector<std::string> history_;
  int max_history_size_ = 100;
  
  std::deque<LogEntry> event_log_;
  int max_log_size_ = 1000;
  
  bool is_awaiting_response_ = false;
  std::string awaiting_message_;
  std::chrono::system_clock::time_point awaiting_start_;
  
  bool retry_available_ = false;
  std::string retry_message_;
  std::string retry_original_message_;
  
  std::atomic<int> ctrl_c_count_{0};
  std::chrono::system_clock::time_point last_ctrl_c_;
  
  int esc_count_ = 0;
  std::chrono::system_clock::time_point last_esc_;
  
  ChatConfig config_;
  std::filesystem::path current_chat_file_;
  std::atomic<size_t> total_tokens_{0};
  
  simdjson::ondemand::parser json_parser_;
  
  /**
   * @brief Client buffer time for timeout calculations
   */
  static constexpr auto CLIENT_BUFFER = std::chrono::seconds(5);
  
  /**
   * @brief Auto-reset delay for stuck requests
   */
  static constexpr auto AUTO_RESET_DELAY = std::chrono::minutes(2);
};