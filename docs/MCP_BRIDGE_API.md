# MCP Bridge Server TCP/JSON API Documentation

## Overview
The MCP Bridge Server communicates with the C++ client via TCP using newline-delimited JSON messages. This document defines the complete API contract between the MCP client and MCP bridge server.

## Connection
- **Protocol**: TCP
- **Port**: 4000 (default)
- **Format**: Newline-delimited JSON (NDJSON)
- **Encoding**: UTF-8

## Message Format
All messages are JSON objects terminated with a newline character (`\n`).

---

## Client → Server Messages

### 1. Chat Message
Send a user message to the AI model.

**Request:**
```json
{
  "type": "chat",
  "content": "User's message text",
  "timeout": 120000,        // Optional: timeout in ms (default: 5 minutes)
  "model": "kimi",          // Optional: "kimi" or "o3" (default: "kimi")
  "reasoning_effort": null  // Optional: for o3 model ("low", "medium", "high")
}
```

**Special Commands:**
- `/new` in content triggers chat history rotation

### 2. Sync Request
Request chat history synchronization check.

**Request:**
```json
{
  "type": "sync",
  "content": "request_history"
}
```

### 3. Reload Request
Request server to reload chat history from file (used after `/load` command).

**Request:**
```json
{
  "type": "reload",
  "content": "chat_history"
}
```

### 4. Tool List Request
Get list of available tools.

**Request:**
```json
{
  "type": "tool_list"
}
```

### 5. Tool Execute Request
Execute a specific tool directly.

**Request:**
```json
{
  "type": "tool_execute",
  "tool_name": "list_files",
  "arguments": {
    "base_path": "/path",
    "recursive": true
  }
}
```

### 6. Retry Request
Retry the last failed message with extended timeout.

**Request:**
```json
{
  "type": "retry",
  "originalMessage": "Previous message text",  // Optional: if not provided, uses last user message
  "model": "kimi",                            // Optional: model to use
  "reasoning_effort": null                    // Optional: for o3 model
}
```

### 7. Reset/Interrupt Request
Cancel active request.

**Request:**
```json
{
  "type": "reset"
}
```

---

## Server → Client Messages

### 1. Welcome Message
Sent immediately upon connection.

**Response:**
```json
{
  "type": "welcome",
  "message": "Connected to MCP bridge server"
}
```

### 2. Chat Response
AI model's response to a chat message.

**Response:**
```json
{
  "type": "response",
  "reply": "AI's response text",
  "original": {...},        // Original request object
  "timestamp": 1234567890
}
```

### 3. Error Response
Error message from server.

**Response:**
```json
{
  "type": "error",
  "message": "Error description",
  "timestamp": 1234567890
}
```

### 4. Timeout Error Response
Timeout with retry option.

**Response:**
```json
{
  "type": "timeout_error",
  "message": "Request timed out after 120 seconds",
  "canRetry": true,
  "originalMessage": "User's original message",
  "timestamp": 1234567890
}
```

### 5. Sync Response
Response to sync request with chat history.

**Response:**
```json
{
  "type": "sync_response",
  "server_stats": "Server: 42 entries",
  "server_history": [
    {"role": "user", "content": "message"},
    {"role": "assistant", "content": "response"}
  ],
  "timestamp": 1234567890
}
```

### 6. Tool List Response
List of available tools.

**Response:**
```json
{
  "type": "tool_list_response",
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "list_files",
        "description": "List files and directories",
        "parameters": {...}
      }
    }
  ],
  "timestamp": 1234567890
}
```

### 7. Tool Execution Result
Result from direct tool execution.

**Response:**
```json
{
  "type": "tool_result",
  "tool_name": "list_files",
  "success": true,
  "result": {...},           // If success=true
  "error": "Error message",  // If success=false
  "timestamp": 1234567890
}
```

### 8. Tool Call Event
Notification when AI calls a tool.

**Response:**
```json
{
  "type": "tool_call",
  "tool_name": "list_files",
  "arguments": {...},
  "timestamp": 1234567890
}
```

### 9. Tool Result Preview
Preview of tool execution results.

**Response:**
```json
{
  "type": "tool_result_preview",
  "tool_name": "list_files",
  "preview": "First 20 lines of output...",
  "total_items": 42,
  "timestamp": 1234567890
}
```

### 10. Tool Error Event
Notification when tool execution fails.

**Response:**
```json
{
  "type": "tool_error",
  "tool_name": "list_files",
  "error": "Permission denied",
  "timestamp": 1234567890
}
```

### 11. Tool Info Message
Information about tool availability.

**Response:**
```json
{
  "type": "tool_info",
  "message": "AI response without tools (1 available)",
  "timestamp": 1234567890
}
```

### 12. Heartbeat
Keep-alive message sent every 5 minutes.

**Response:**
```json
{
  "type": "heartbeat",
  "message": "Server is alive",
  "timestamp": 1234567890
}
```

---

## Message Flow Examples

### Basic Chat
```
Client → {"type":"chat","content":"Hello"}
Server → {"type":"response","reply":"Hello! How can I help?","timestamp":123}
```

### Chat with Tool Use
```
Client → {"type":"chat","content":"List files in current directory"}
Server → {"type":"tool_call","tool_name":"list_files","arguments":{"base_path":"."},"timestamp":123}
Server → {"type":"tool_result_preview","tool_name":"list_files","preview":"file1.txt\nfile2.txt","total_items":2,"timestamp":124}
Server → {"type":"response","reply":"I found 2 files: file1.txt and file2.txt","timestamp":125}
```

### Timeout and Retry
```
Client → {"type":"chat","content":"Complex question","timeout":30000}
Server → {"type":"timeout_error","message":"Request timed out after 30 seconds","canRetry":true,"originalMessage":"Complex question","timestamp":123}
Client → {"type":"retry","originalMessage":"Complex question"}
Server → {"type":"response","reply":"Here's the answer...","timestamp":124}
```

### Load Conversation
```
# Client performs /load 2 command internally
# Client loads chat-history-2024-12-01.json → chat-history.json
Client → {"type":"reload","content":"chat_history"}
# Server reloads chat-history.json into memory
```

---

## Error Handling

1. **Malformed JSON**: Server logs error, connection continues
2. **Unknown message type**: Server logs and skips message
3. **Connection lost**: Client automatically reconnects with exponential backoff
4. **Request timeout**: Server sends timeout_error with retry option

---

## State Management

### Client State
- Chat history file management
- UI event log
- Connection status
- Retry availability

### Server State
- In-memory chat history
- Active request tracking (AbortController per client)
- Model configuration
- Tool availability

---

## Notes

1. The server does NOT use JSON-RPC 2.0 format (no `id`, `method`, `params`)
2. All timestamps are Unix milliseconds
3. Chat history is persisted by C++ client, server only maintains memory copy
4. Server must be notified via `reload` after client loads different conversation
5. Tool calls are automatic based on AI model's decision, not client-initiated