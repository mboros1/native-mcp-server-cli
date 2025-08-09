#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <json_struct/json_struct.h>

// Core types without any UI dependencies
// This file can be used by both UI and headless builds

// ============================================================================
// Communication Mode
// ============================================================================
enum class CommMode {
  STANDALONE,  // Direct terminal interaction
  IPC         // Communicating with Node.js wrapper via JSON-RPC
};

// ============================================================================
// Log Entry Types
// ============================================================================
// JS_ENUM for serialization (also declares the enum)
JS_ENUM(LogEntryType, USER, SYSTEM, RESPONSE, ERROR, BLOCKED)
JS_ENUM_DECLARE_STRING_PARSER(LogEntryType)

// ============================================================================
// Data Models
// ============================================================================
struct ToolParameter {
  std::string name;
  std::string type;
  std::string description;
  bool required;
  
  JS_OBJ(name, type, description, required);
};

struct Tool {
  std::string name;
  std::string description;
  std::string category;
  std::vector<ToolParameter> parameters;
  std::vector<std::string> examples;
  
  JS_OBJ(name, description, category, parameters, examples);
};

struct Config {
  std::string welcomeMessage = "◆ TERMINAL CHAT ◆";
  std::string inputPlaceholder = "Type your message...";
  std::string serverName = "MCP Bridge Server";
  std::string serverVersion = "1.0.0";
  int maxHistorySize = 50;
  std::vector<Tool> tools;
  
  JS_OBJ(welcomeMessage, inputPlaceholder, serverName, serverVersion, maxHistorySize, tools);
};

struct LogEntry {
  LogEntryType type;
  std::string content;
  std::chrono::system_clock::time_point timestamp;
  
  JS_OBJ(type, content, timestamp);
};

// Chat history entry for persistence
struct ChatHistoryEntry {
  std::string role;
  std::string content;
  size_t token_cnt;
  std::chrono::system_clock::time_point timestamp;
  
  JS_OBJ(role, content, token_cnt, timestamp);
};

// Chat configuration
struct ChatConfig {
  std::string model = "kimi";
  std::string reasoning_effort = "medium";
  
  JS_OBJ(model, reasoning_effort);
};

// Chat request structure
struct ChatRequest {
  std::string type = "chat";
  int id;
  std::string content;
  std::string model;
  std::string reasoning_effort = "medium";
  int timeout = 30000;
  
  JS_OBJ(type, id, content, model, reasoning_effort, timeout);
};

// Interrupt request
struct InterruptRequest {
  std::string type = "interrupt";
  
  JS_OBJ(type);
};

// Retry request
struct RetryRequest {
  std::string type = "retry";
  std::string originalMessage;
  std::string model;
  std::string reasoning_effort = "medium";
  
  JS_OBJ(type, originalMessage, model, reasoning_effort);
};

// ============================================================================
// Timestamp Helper
// ============================================================================
namespace JS {
template<>
struct TypeHandler<std::chrono::system_clock::time_point> {
  static inline Error to(std::chrono::system_clock::time_point& to_type, ParseContext& context) {
    std::string time_str;
    auto error = TypeHandler<std::string>::to(time_str, context);
    if (error != Error::NoError) return error;
    
    // Parse ISO 8601 timestamp - simplified version
    std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    
    if (ss.fail()) {
      return Error::UnknownError;
    }
    
    // Convert to time_point
    to_type = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    
    // Parse milliseconds if present
    if (time_str.find('.') != std::string::npos) {
      size_t dot_pos = time_str.find('.');
      size_t z_pos = time_str.find('Z');
      if (z_pos != std::string::npos && dot_pos < z_pos) {
        std::string ms_str = time_str.substr(dot_pos + 1, z_pos - dot_pos - 1);
        if (ms_str.length() >= 3) {
          int ms = std::stoi(ms_str.substr(0, 3));
          to_type += std::chrono::milliseconds(ms);
        }
      }
    }
    
    return Error::NoError;
  }
  
  static inline void from(const std::chrono::system_clock::time_point& from_type, 
                          Token& token, Serializer& serializer) {
    // Convert to ISO 8601 string with milliseconds
    auto time_t = std::chrono::system_clock::to_time_t(from_type);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      from_type.time_since_epoch()) % 1000;
    
    char time_str[30];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", std::gmtime(&time_t));
    
    std::string iso_time = std::string(time_str) + "." + 
                          std::to_string(ms.count() / 100) + 
                          std::to_string((ms.count() / 10) % 10) + 
                          std::to_string(ms.count() % 10) + "Z";
    
    TypeHandler<std::string>::from(iso_time, token, serializer);
  }
};
}