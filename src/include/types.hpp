#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <json_struct/json_struct.h>
#include <ftxui/screen/color.hpp>

using namespace ftxui;

// ============================================================================
// Color Palette
// ============================================================================
namespace Colors {
  const auto kBackground = Color::RGB(0x0D, 0x0F, 0x0F);
  const auto kDarkBg = Color::RGB(0x1E, 0x1F, 0x29);
  const auto kGreen = Color::RGB(0x72, 0xF1, 0xB8);
  const auto kBrightGreen = Color::RGB(0x00, 0xFF, 0x9C);
  const auto kDimGreen = Color::RGB(0x66, 0xFF, 0x66);
  const auto kHotPink = Color::RGB(0xFF, 0x2D, 0x95);
  const auto kPink = Color::RGB(0xFF, 0x7E, 0xDB);
  const auto kPurple = Color::RGB(0x9B, 0x5D, 0xF5);
  const auto kCyan = Color::RGB(0x5A, 0xF7, 0x8E);
  const auto kGray = Color::RGB(0x88, 0x88, 0x88);
}

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
  std::string defaultValue;
};

struct Tool {
  std::string name;
  std::string description;
  std::string category;
  std::vector<ToolParameter> parameters;
  std::vector<std::string> examples;
};

struct Config {
  std::string welcomeMessage = "🚀 Hello from FTXUI!";
  std::string inputPlaceholder = "Type here…";
  std::string serverName = "MCP Server";
  std::string serverVersion = "1.0.0";
  int maxHistorySize = 100;
  bool enableLogging = false;
  std::vector<Tool> tools;
};

// ============================================================================
// Chat History Structures
// ============================================================================
struct ChatHistoryEntry {
  std::string role;
  std::string content;
  size_t token_cnt;
  std::chrono::system_clock::time_point timestamp;
  
  JS_OBJECT(
    JS_MEMBER(role),
    JS_MEMBER(content),
    JS_MEMBER(token_cnt),
    JS_MEMBER(timestamp)
  );
};

struct LogEntry {
  LogEntryType type;
  std::string content;
  std::chrono::system_clock::time_point timestamp;
  
  JS_OBJECT(
    JS_MEMBER(type),
    JS_MEMBER(content),
    JS_MEMBER(timestamp)
  );
};

// ============================================================================
// Request/Response Structures
// ============================================================================
struct ChatConfig {
  std::string model = "kimi";
  std::string reasoning_effort = "medium";
  
  JS_OBJECT(
    JS_MEMBER(model),
    JS_MEMBER(reasoning_effort)
  );
};

struct ChatRequest {
  int id;
  std::string type = "chat";
  std::string content;
  std::string model;
  std::string reasoning_effort;
  int64_t timeout;
  
  JS_OBJECT(
    JS_MEMBER(id),
    JS_MEMBER(type),
    JS_MEMBER(content),
    JS_MEMBER(model),
    JS_MEMBER(reasoning_effort),
    JS_MEMBER(timeout)
  );
};

struct RetryRequest {
  std::string type = "retry";
  std::string originalMessage;
  std::string model;
  std::string reasoning_effort;
  
  JS_OBJECT(
    JS_MEMBER(type),
    JS_MEMBER(originalMessage),
    JS_MEMBER(model),
    JS_MEMBER(reasoning_effort)
  );
};

struct InterruptRequest {
  std::string type = "interrupt";
  
  JS_OBJECT(
    JS_MEMBER(type)
  );
};

// ============================================================================
// Timeout State
// ============================================================================
enum class TimeoutState {
  NORMAL,      // Within timeout bounds
  WARNING,     // Approaching timeout (>75% of limit)  
  EXCEEDED,    // Timeout exceeded, can still wait
  AUTO_RESET   // Auto-reset period, will clear soon
};

// ============================================================================
// Custom TypeHandler for chrono::time_point serialization
// ============================================================================
namespace JS {
template <>
struct TypeHandler<std::chrono::system_clock::time_point> {
  static Error to(std::chrono::system_clock::time_point& to_type, ParseContext& context) {
    if (context.token.value_type == Type::String) {
      std::string time_str;
      auto err = TypeHandler<std::string>::to(time_str, context);
      if (err != Error::NoError) return err;
      
      // Parse ISO 8601 string (simplified - assumes format)
      std::tm tm = {};
      std::istringstream ss(time_str);
      ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
      
      if (ss.fail()) {
        return Error::UserDefinedErrors;
      }
      
      to_type = std::chrono::system_clock::from_time_t(std::mktime(&tm));
      return Error::NoError;
    }
    return Error::ExpectedDataToken;
  }
  
  static void from(const std::chrono::system_clock::time_point& from_type,
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