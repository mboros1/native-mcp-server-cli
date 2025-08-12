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
  SPDLOG_INFO("HandleServerMessage called with: {}", message.substr(0, 200));
  
  // Allow processing if either legacy callback or registry has handlers
  if (!response_callback_ && registry_.GetPendingCount() == 0) {
    SPDLOG_WARN("No response callback set and no pending registry requests");
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
    
    // Check if this is a JSON-RPC 2.0 message
    if (doc["jsonrpc"].is_string() && std::string(doc["jsonrpc"].get_string().value()) == "2.0") {
      SPDLOG_INFO("Detected JSON-RPC 2.0 message");
      HandleJsonRpcMessage(doc);
      return;
    }
    
    // Legacy format handling (for backward compatibility during migration)
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
    
  } catch (const simdjson::simdjson_error& e) {
    SPDLOG_ERROR("SimdJSON error processing server message: {}", e.what());
    SPDLOG_ERROR("Message was: {}", message.substr(0, 500));
  } catch (const std::exception& e) {
    SPDLOG_ERROR("Error processing server message: {}", e.what());
    SPDLOG_ERROR("Message was: {}", message.substr(0, 500));
  }
}

void MCPClient::HandleJsonRpcMessage(const dom::element& doc) {
  SPDLOG_DEBUG("Processing JSON-RPC 2.0 message");
  
  try {
    // Check if this is a response (has id) or notification (no id or id is null)
    bool has_id = doc["id"].is_int64() || doc["id"].is_uint64();
    
    if (has_id) {
      // This is a response to our request
      int64_t id = doc["id"].is_int64() ? doc["id"].get_int64().value() : 
                   static_cast<int64_t>(doc["id"].get_uint64().value());
      
      SPDLOG_DEBUG("Received JSON-RPC response with id: {}", id);
      
      // Notify InputHandler about the response ID for correlation
      if (id_callback_) {
        id_callback_(id);
      }
      
      // Check if it's an error response - error field exists and is not null
      simdjson::dom::element error_elem;
      auto error_lookup = doc.at_key("error").get(error_elem);
      if (error_lookup == simdjson::SUCCESS && !error_elem.is_null()) {
        SPDLOG_INFO("Processing error response");
        HandleJsonRpcError(error_elem, id);
        return;
      }
      
      // Process successful result - result field exists and is not null
      simdjson::dom::element result_elem;
      auto result_lookup = doc.at_key("result").get(result_elem);
      if (result_lookup == simdjson::SUCCESS && !result_elem.is_null()) {
        SPDLOG_INFO("Processing result response");
        // Note: responses don't have a method field
        simdjson::dom::element empty;
        HandleJsonRpcResult(result_elem, empty, id);
      }
    } else {
      // This is a notification or request from server
      SPDLOG_INFO("Processing notification/request");
      if (doc["method"].is_string()) {
        std::string method = std::string(doc["method"].get_string().value());
        HandleJsonRpcNotification(method, doc["params"]);
      }
    }
  } catch (const simdjson::simdjson_error& e) {
    SPDLOG_ERROR("SimdJSON error in HandleJsonRpcMessage: {}", e.what());
    throw;
  }
}

void MCPClient::HandleJsonRpcError(const dom::element& error, int64_t id) {
  int code = error["code"].is_int64() ? error["code"].get_int64().value() : 0;
  std::string message = error["message"].is_string() ? 
    std::string(error["message"].get_string().value()) : "Unknown error";
  
  SPDLOG_ERROR("JSON-RPC error (id: {}): [{}] {}", id, code, message);
  
  // Try the registry first
  if (registry_.HandleError(id, code, message)) {
    // Remove from pending requests
    pending_requests_.erase(id);
    return;
  }
  
  // Fall back to legacy callback handling
  // Debug: Print what data looks like
  if (!error["data"].is_null()) {
    if (error["data"].is_string()) {
      SPDLOG_INFO("Error data is a string: {}", std::string(error["data"].get_string().value()));
    } else if (error["data"].is_object()) {
      SPDLOG_INFO("Error data is an object");
    } else {
      SPDLOG_INFO("Error data is of unknown type");
    }
  }
  
  // Map error codes to response types
  if (code == -32001) {  // Timeout
    // Check if we can retry - data is a JSON string that needs parsing
    if (error["data"].is_string()) {
      std::string data_str = std::string(error["data"].get_string().value());
      // Parse the JSON string
      dom::parser data_parser;
      dom::element data_doc;
      auto parse_error = data_parser.parse(data_str).get(data_doc);
      if (!parse_error && data_doc["canRetry"].is_bool() && data_doc["canRetry"].get_bool().value()) {
        std::string original = data_doc["originalMessage"].is_string() ?
          std::string(data_doc["originalMessage"].get_string().value()) : "";
        response_callback_("TIMEOUT_WITH_RETRY", message + "|" + original);
      } else {
        response_callback_("ERROR", "Timeout: " + message);
      }
    } else {
      response_callback_("ERROR", "Timeout: " + message);
    }
  } else {
    response_callback_("ERROR", "Error [" + std::to_string(code) + "]: " + message);
  }
}

void MCPClient::HandleJsonRpcResult(const dom::element& result, const dom::element&, int64_t id) {
  // Note: method_elem is empty for JSON-RPC responses (they don't have a method field)
  // We determine the result type by examining the structure of the result
  
  SPDLOG_DEBUG("Processing result (id: {})", id);
  
  // Try the registry first
  SPDLOG_INFO("Trying registry for ID: {} (pending count: {})", id, registry_.GetPendingCount());
  if (registry_.HandleResponse(id, result)) {
    SPDLOG_INFO("Registry handled response for ID: {}", id);
    // Remove from pending requests
    pending_requests_.erase(id);
    return;
  } else {
    SPDLOG_INFO("Registry did not handle response for ID: {}", id);
  }
  
  // Fall back to legacy parsing
  // Debug: Print what fields exist in the result
  SPDLOG_INFO("Analyzing result structure...");
  
  // Chat response - check if reply field exists
  simdjson::dom::element reply_elem;
  auto reply_lookup = result.at_key("reply").get(reply_elem);
  if (reply_lookup == simdjson::SUCCESS) {
    SPDLOG_INFO("Found reply field, checking if it's a string");
    if (reply_elem.is_string()) {
      SPDLOG_INFO("Reply is a string, extracting value");
      std::string_view reply_view;
      auto string_error = reply_elem.get_string().get(reply_view);
      if (string_error == simdjson::SUCCESS) {
        std::string reply(reply_view);
        SPDLOG_INFO("Received chat response: {}", reply);
    
        // Check if this is a system message
        if (reply.find("Started new conversation") != std::string::npos || 
            reply.find("Chat history has been rotated") != std::string::npos) {
          response_callback_("SYSTEM", reply);
        } else {
          response_callback_("RESPONSE", reply);
        }
        return;
      }
    }
  }
  
  // Sync response - check for entries and messages fields
  simdjson::dom::element entries_elem, messages_elem;
  auto entries_lookup = result.at_key("entries").get(entries_elem);
  auto messages_lookup = result.at_key("messages").get(messages_elem);
  if (entries_lookup == simdjson::SUCCESS && entries_elem.is_int64() && 
      messages_lookup == simdjson::SUCCESS && messages_elem.is_array()) {
    // Convert back to JSON string for the callback
    std::string sync_json = simdjson::minify(result);
    response_callback_("SYNC", sync_json);
    return;
  }
  
  // Tool list response - check for tools array
  simdjson::dom::element tools_elem;
  auto tools_lookup = result.at_key("tools").get(tools_elem);
  if (tools_lookup == simdjson::SUCCESS && tools_elem.is_array()) {
    std::string tools_json = simdjson::minify(result);
    response_callback_("TOOLS", tools_json);
    return;
  }
  
  // Simple success response - check for success field
  simdjson::dom::element success_elem;
  auto success_lookup = result.at_key("success").get(success_elem);
  if (success_lookup == simdjson::SUCCESS && success_elem.is_bool()) {
    bool success = success_elem.get_bool().value();
    
    // Check for optional message
    simdjson::dom::element msg_elem;
    auto msg_lookup = result.at_key("message").get(msg_elem);
    std::string msg = (msg_lookup == simdjson::SUCCESS && msg_elem.is_string()) ?
      std::string(msg_elem.get_string().value()) : 
      (success ? "Operation successful" : "Operation failed");
    
    if (success) {
      response_callback_("SYSTEM", msg);
    } else {
      response_callback_("ERROR", msg);
    }
    return;
  }
  
  // Generic result - pass as JSON
  std::string result_json = simdjson::minify(result);
  response_callback_("RESULT", result_json);
}

void MCPClient::HandleJsonRpcNotification(const std::string& method, const dom::element& params) {
  SPDLOG_DEBUG("Received JSON-RPC notification: {}", method);
  
  // Tool notifications
  if (method == "tool.called") {
    std::string tool_name = params["tool_name"].is_string() ? 
      std::string(params["tool_name"].get_string().value()) : "unknown";
    
    std::string args_str = "{}";
    if (params["arguments"].is_string()) {
      args_str = std::string(params["arguments"].get_string().value());
    }
    
    std::string log_msg = "🔧 Tool call: " + tool_name + " with args: " + args_str;
    SPDLOG_INFO("Tool call: {}", log_msg);
    response_callback_("TOOL_EVENT", log_msg);
  }
  else if (method == "tool.result") {
    std::string tool_name = params["tool_name"].is_string() ? 
      std::string(params["tool_name"].get_string().value()) : "unknown";
    std::string preview = params["preview"].is_string() ? 
      std::string(params["preview"].get_string().value()) : "";
    int total_items = params["total_items"].is_int64() ? 
      params["total_items"].get_int64().value() : 0;
    
    std::string log_msg = "📋 Tool result for " + tool_name + ":\n" + preview;
    if (total_items > 0) {
      log_msg += "\n(Total items: " + std::to_string(total_items) + ")";
    }
    SPDLOG_INFO("Tool result preview: {}", tool_name);
    response_callback_("TOOL_EVENT", log_msg);
  }
  else if (method == "stream.chunk") {
    std::string stream_id = params["streamId"].is_string() ? 
      std::string(params["streamId"].get_string().value()) : "";
    std::string chunk = params["chunk"].is_string() ? 
      std::string(params["chunk"].get_string().value()) : "";
    
    // For streaming, we'll append chunks to the response
    response_callback_("STREAM_CHUNK", chunk);
  }
  else if (method == "stream.end") {
    std::string stream_id = params["streamId"].is_string() ? 
      std::string(params["streamId"].get_string().value()) : "";
    int total_chunks = params["totalChunks"].is_int64() ? 
      params["totalChunks"].get_int64().value() : 0;
    
    SPDLOG_INFO("Stream {} ended with {} chunks", stream_id, total_chunks);
    response_callback_("STREAM_END", std::to_string(total_chunks));
  }
  else if (method == "heartbeat") {
    // Ignore heartbeat notifications
    SPDLOG_DEBUG("Received heartbeat");
  }
  else {
    SPDLOG_WARN("Unknown notification method: {}", method);
  }
}
