#ifndef INPUT_HANDLER_V2_HPP
#define INPUT_HANDLER_V2_HPP

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <set>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <json_struct.h>
#include <fmt/chrono.h>
#include "../include/types.hpp"
#include "../include/app_events.hpp"  // Use our abstracted events
#include "state_manager.hpp"
#include "../network/mcp_client.hpp"

// Forward declarations to avoid circular dependencies
class UIRenderer;
class ConversationLogManager;

/**
 * InputHandler with abstracted event system
 * This version has no FTXUI dependencies
 */
class InputHandler {
public:
  const std::string& model()  const { return config_.model;  }
  const std::string& effort() const { return config_.reasoning_effort; }
  
private:
  ChatConfig config_;  // Configuration loaded from/saved to disk
  StateManager& state_;
  std::vector<Tool>& tools_;
  UIRenderer* renderer_ = nullptr;
  ConversationLogManager* log_manager_ = nullptr;
  CommMode comm_mode_ = CommMode::STANDALONE;
  MCPClient* mcp_client_ = nullptr;
  int message_id_ = 0;
  
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
  
  /**
   * Handle application events (UI-framework agnostic)
   * Returns true if the event was handled
   */
  bool HandleEvent(const app::Event& event);
};

#endif // INPUT_HANDLER_V2_HPP