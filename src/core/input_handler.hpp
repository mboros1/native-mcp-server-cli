#ifndef INPUT_HANDLER_HPP
#define INPUT_HANDLER_HPP

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <set>
#include <map>
#include <optional>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <json_struct.h>
#include <fmt/chrono.h>
#include "../include/types_core.hpp"
#include "../include/app_events.hpp"
#include "../protocol/jsonrpc_messages.hpp"
#include "../protocol/jsonrpc_procedures.hpp"
#include "state_manager.hpp"
#include "../network/mcp_client.hpp"
// Forward declarations to avoid UI dependencies
class UIRenderer;
class ConversationLogManager;

// No FTXUI dependency

class InputHandler {
public:
  const std::string& model()  const { return config_.model;  }
  const std::string& effort() const { return config_.reasoning_effort; }
  
  // Notification callback for headless mode
  using NotificationCallback = std::function<void(LogEntryType, const std::string&)>;
  
private:
  ChatConfig config_;  // Configuration loaded from/saved to disk
  StateManager& state_;
  std::vector<Tool>& tools_;
  UIRenderer* renderer_ = nullptr;
  ConversationLogManager* log_manager_ = nullptr;
  CommMode comm_mode_ = CommMode::STANDALONE;
  MCPClient* mcp_client_ = nullptr;
  NotificationCallback notification_callback_ = nullptr;
  
  // JSON-RPC 2.0 request tracking
  int next_request_id_ = 0;  // Sequential ID generator
  std::map<int, std::string> pending_requests_;  // Track what each ID is for
  std::optional<int> current_chat_id_;  // Track active chat request for cancellation
  
  Tool* FindTool(const std::string& name);
  void DumpScreen();
  std::string EscapeJSON(const std::string& str);
  
public:
  bool sync_retry_enabled_ = true;  // Track retry state for sync operations
  
  InputHandler(StateManager& state, std::vector<Tool>& tools);
  
  void SetRenderer(UIRenderer* renderer);
  void SetLogManager(ConversationLogManager* manager);
  void SetCommMode(CommMode mode);
  void SetMCPClient(MCPClient* client);
  void SetNotificationCallback(NotificationCallback callback);
  
  void LoadConfig();
  void SaveConfig();
  
  void RequestSyncCheck(bool retry = true);
  void AddLogEntryWithNotification(LogEntryType type, const std::string& content);
  
  void SendInterrupt();
  void SendRetryRequest();
  void SendChatMessage(const std::string& message);
  void SendMCPRequest(const std::string& method, const std::string& params);
  void SendToolCall(const std::string& toolName, const std::string& args);
  
  void ProcessCommand(const std::string& command);
  bool HandleEvent(const app::Event& event);
  
  // Response tracking for JSON-RPC 2.0
  void OnResponseReceived(int id);
};

#endif // INPUT_HANDLER_HPP
