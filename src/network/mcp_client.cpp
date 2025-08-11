#include "mcp_client.hpp"

MCPClient::MCPClient(const std::string& host, int port) 
  : event_queue_(1024), host_(host), port_(port) {}

MCPClient::~MCPClient() {
  Stop();
}

bool MCPClient::Connect() {
  SPDLOG_INFO("Connecting to TCP server at {}:{}", host_, port_);
  
  tcp_client_ = std::make_unique<TcpClient>(event_queue_);
  
  // Start event processor thread before attempting connection
  running_ = true;
  event_processor_thread_ = std::thread([this]() { ProcessEvents(); });
  
  // Start TCP client connection attempts
  tcp_client_->start(host_, port_);
  
  // Wait a bit to see if initial connection succeeds
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  
  if (tcp_client_->is_connected()) {
    SPDLOG_INFO("Connected to TCP server");
    return true;
  } else {
    SPDLOG_WARN("Initial connection failed, will keep retrying in background");
    return false;
  }
}

bool MCPClient::IsConnected() const {
  return tcp_client_ && tcp_client_->is_connected();
}

void MCPClient::Stop() {
  SPDLOG_INFO("Stopping MCP Server");
  running_ = false;
  
  if (tcp_client_) {
    tcp_client_->stop();
  }
  
  if (event_processor_thread_.joinable()) {
    event_processor_thread_.join();
  }
}

void MCPClient::SendRequest(const std::string& json) {
  if (!tcp_client_ || !tcp_client_->is_connected()) {
    SPDLOG_ERROR("Cannot send request - not connected");
    return;
  }
  
  SPDLOG_INFO("Sending to server: {}", json);
  tcp_client_->send_message(json);
}

void MCPClient::SetResponseCallback(std::function<void(const std::string&, const std::string&)> callback) {
  response_callback_ = callback;
}

void MCPClient::ProcessEvents() {
  while (running_) {
    AppEvent event;
    if (event_queue_.try_pop(event)) {
      switch (event.type) {
        case EventType::Connected:
          SPDLOG_INFO("TCP connection established: {}", event.data);
          break;
        case EventType::ConnectionFailed:
          SPDLOG_WARN("TCP connection failed: {}", event.data);
          break;
        case EventType::ConnectionLost:
          SPDLOG_WARN("TCP connection lost: {}", event.data);
          break;
        case EventType::MessageReceived:
          SPDLOG_DEBUG("Received from server: {}", event.data);
          HandleServerMessage(event.data);
          break;
        default:
          break;
      }
    } else {
      // Sleep briefly if no events available
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  SPDLOG_INFO("Event processor thread exiting");
}

void MCPClient::HandleServerMessage(const std::string& message) {
  SPDLOG_DEBUG("HandleServerMessage called with: {}", message.substr(0, 100));
  
  if (!response_callback_) {
    SPDLOG_WARN("No response callback set, cannot process server message");
    return;
  }
  
  try {
    // Parse JSON response from server
    dom::parser parser;
    dom::element doc;
    
    auto error = parser.parse(message).get(doc);
    if (error) {
      SPDLOG_ERROR("Failed to parse server message: {}", error_message(error));
      return;
    }
    
    // Check message type
    if (doc["type"].is_string()) {
      std::string type = std::string(doc["type"].get_string().value());
      SPDLOG_DEBUG("Received message type: {}", type);
      
      if (type == "response") {
        if (doc["reply"].is_string()) {
          std::string reply = std::string(doc["reply"].get_string().value());
          SPDLOG_INFO("Received chat response: {}", reply);
          
          // Check if this is a system message (like /new command response)
          if (reply.find("Started new conversation") != std::string::npos || 
              reply.find("Chat history has been rotated") != std::string::npos) {
            response_callback_("SYSTEM", reply);
          } else {
            response_callback_("RESPONSE", reply);
          }
        }
      } else if (type == "sync_response") {
        // Pass sync response to callback for processing
        response_callback_("SYNC", message);
      } else if (type == "timeout_error") {
        // Handle timeout errors with retry capability
        if (doc["message"].is_string() && doc["canRetry"].is_bool() && doc["originalMessage"].is_string()) {
          std::string timeout_msg = std::string(doc["message"].get_string().value());
          bool can_retry = doc["canRetry"].get_bool().value();
          std::string original_msg = std::string(doc["originalMessage"].get_string().value());
          
          SPDLOG_ERROR("Received timeout from server: {}", timeout_msg);
          
          if (can_retry) {
            response_callback_("TIMEOUT_WITH_RETRY", timeout_msg + "|" + original_msg);
          } else {
            response_callback_("ERROR", "Timeout: " + timeout_msg);
          }
        }
      } else if (type == "error") {
        if (doc["message"].is_string()) {
          std::string error_msg = std::string(doc["message"].get_string().value());
          SPDLOG_ERROR("Received error from server: {}", error_msg);
          response_callback_("ERROR", "Server error: " + error_msg);
        }
      } else if (type == "tool_call") {
        // Handle tool call event
        std::string tool_name = doc["tool_name"].is_string() ? 
          std::string(doc["tool_name"].get_string().value()) : "unknown";
        
        std::string args_str = "{}";
        if (doc["arguments"].is_object()) {
          dom::element args = doc["arguments"].value();
          args_str = simdjson::minify(args);
        }
        
        std::string log_msg = "🔧 Tool call: " + tool_name + " with args: " + args_str;
        SPDLOG_INFO("Tool call: {}", log_msg);
        response_callback_("TOOL_EVENT", log_msg);
        
      } else if (type == "tool_result_preview") {
        // Handle tool result preview
        std::string tool_name = doc["tool_name"].is_string() ? 
          std::string(doc["tool_name"].get_string().value()) : "unknown";
        std::string preview = doc["preview"].is_string() ? 
          std::string(doc["preview"].get_string().value()) : "";
        int64_t total_items = doc["total_items"].is_int64() ? 
          doc["total_items"].get_int64().value() : 0;
        
        std::string log_msg = "📋 Tool result for " + tool_name + ":\n" + preview;
        if (total_items > 0) {
          log_msg += "\n(Total items: " + std::to_string(total_items) + ")";
        }
        SPDLOG_INFO("Tool result preview: {}", tool_name);
        response_callback_("TOOL_EVENT", log_msg);
        
      } else if (type == "tool_error") {
        // Handle tool error
        std::string tool_name = doc["tool_name"].is_string() ? 
          std::string(doc["tool_name"].get_string().value()) : "unknown";
        std::string error = doc["error"].is_string() ? 
          std::string(doc["error"].get_string().value()) : "Unknown error";
        
        std::string log_msg = "❌ Tool error for " + tool_name + ": " + error;
        SPDLOG_ERROR("Tool error: {}", log_msg);
        response_callback_("TOOL_EVENT", log_msg);
        
      } else if (type == "tool_info") {
        // Handle tool info (when AI doesn't call tools)
        std::string msg = doc["message"].is_string() ? 
          std::string(doc["message"].get_string().value()) : "";
        
        std::string log_msg = "ℹ️ " + msg;
        SPDLOG_INFO("Tool info: {}", msg);
        response_callback_("TOOL_EVENT", log_msg);
      }
    } else {
      SPDLOG_DEBUG("Received non-chat message: {}", message);
    }
    
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Error processing server message: {}", e.what());
  }
}