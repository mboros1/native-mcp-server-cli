# JSON-RPC 2.0 Migration Plan for MCP Bridge API

## Executive Summary
This document outlines the migration from our current custom message format to the JSON-RPC 2.0 standard, mapping each current message type to its JSON-RPC 2.0 equivalent.

## Benefits of Migration
1. **Request/Response Correlation**: Every request gets a unique ID for tracking
2. **Standard Error Handling**: Consistent error format with codes
3. **Batch Support**: Send multiple requests in one message
4. **Better Tooling**: Standard format supported by many libraries
5. **Clear Contract**: Well-defined protocol reduces ambiguity

## Message Type Mappings

### Client → Server Messages

#### 1. Chat Message
**Current Format:**
```json
{
  "type": "chat",
  "content": "User's message",
  "timeout": 120000,
  "model": "kimi",
  "reasoning_effort": null
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "chat.send",
  "params": {
    "content": "User's message",
    "timeout": 120000,
    "model": "kimi",
    "reasoning_effort": null
  }
}
```

#### 2. Sync Request
**Current Format:**
```json
{
  "type": "sync",
  "content": "request_history"
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "history.sync",
  "params": {}
}
```

#### 3. Reload Request
**Current Format:**
```json
{
  "type": "reload",
  "content": "chat_history"
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "method": "history.reload",
  "params": {}
}
```

#### 4. Tool List Request
**Current Format:**
```json
{
  "type": "tool_list"
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 4,
  "method": "tools.list",
  "params": {}
}
```

#### 5. Tool Execute Request
**Current Format:**
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

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 5,
  "method": "tools.execute",
  "params": {
    "name": "list_files",
    "arguments": {
      "base_path": "/path",
      "recursive": true
    }
  }
}
```

#### 6. Retry Request
**Current Format:**
```json
{
  "type": "retry",
  "originalMessage": "Previous message",
  "model": "kimi",
  "reasoning_effort": null
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 6,
  "method": "chat.retry",
  "params": {
    "originalMessage": "Previous message",
    "model": "kimi",
    "reasoning_effort": null
  }
}
```

#### 7. Reset/Interrupt Request
**Current Format:**
```json
{
  "type": "reset"
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 7,
  "method": "request.cancel",
  "params": {
    "requestId": null  // null = cancel current, or specific ID
  }
}
```

### Server → Client Messages

#### 1. Success Responses
**Current Format:**
```json
{
  "type": "response",
  "reply": "AI's response",
  "original": {...},
  "timestamp": 1234567890
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "reply": "AI's response",
    "timestamp": 1234567890
  }
}
```

#### 2. Error Responses
**Current Format:**
```json
{
  "type": "error",
  "message": "Error description",
  "timestamp": 1234567890
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32000,
    "message": "Error description",
    "data": {
      "timestamp": 1234567890
    }
  }
}
```

#### 3. Timeout Error
**Current Format:**
```json
{
  "type": "timeout_error",
  "message": "Request timed out after 120 seconds",
  "canRetry": true,
  "originalMessage": "User's message",
  "timestamp": 1234567890
}
```

**JSON-RPC 2.0 Format:**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "error": {
    "code": -32001,  // Custom timeout error code
    "message": "Request timed out after 120 seconds",
    "data": {
      "canRetry": true,
      "originalMessage": "User's message",
      "timestamp": 1234567890
    }
  }
}
```

#### 4. Notifications (No Response Expected)
**Current Format:**
```json
{
  "type": "tool_call",
  "tool_name": "list_files",
  "arguments": {...},
  "timestamp": 1234567890
}
```

**JSON-RPC 2.0 Format (Notification - no id):**
```json
{
  "jsonrpc": "2.0",
  "method": "tool.called",
  "params": {
    "tool_name": "list_files",
    "arguments": {...},
    "timestamp": 1234567890
  }
}
```

## Error Code Standards

| Code Range | Purpose | Example |
|------------|---------|---------|
| -32700 to -32600 | Parse/Protocol errors | Invalid JSON |
| -32601 | Method not found | Unknown RPC method |
| -32602 | Invalid params | Missing required param |
| -32603 | Internal error | Server crash |
| -32000 to -32099 | Server errors | Our custom errors |

### Our Custom Error Codes
```
-32000: General server error
-32001: Request timeout
-32002: Model unavailable
-32003: Tool execution failed
-32004: Rate limit exceeded
-32005: Authentication failed
-32010: Chat history sync error
-32011: File operation error
```

## Implementation Phases

### Phase 1: Dual Protocol Support (Week 1-2)
1. Add JSON-RPC 2.0 parser alongside existing parser
2. Detect message format by presence of `jsonrpc` field
3. Internal message routing remains unchanged
4. Add request ID tracking map
5. **Add version negotiation on connect** (from API_IMPROVEMENTS.md)
6. **Implement streaming support foundation** (from API_IMPROVEMENTS.md)

**Code Changes:**
- `server/lib/messageParser.js` - Detect and parse both formats
- `server/lib/requestTracker.js` - Map request IDs to responses
- `src/network/mcp_client.cpp` - Parse both response formats
- `src/protocol/jsonrpc_types.hpp` - Strongly-typed C++ message classes
- `src/protocol/jsonrpc_client.hpp` - JSON-RPC client implementation

### Phase 2: Server Migration (Week 3-4)
1. Convert internal handlers to use JSON-RPC format
2. Add proper error code handling
3. Implement notification vs request logic
4. Add batch request support
5. **Add rate limiting information in responses** (from API_IMPROVEMENTS.md)
6. **Implement trace IDs for debugging** (from API_IMPROVEMENTS.md)
7. **Add JSON Schema validation** (from API_IMPROVEMENTS.md)

**Code Changes:**
- `server/lib/messageHandlers.js` - Use JSON-RPC internally
- `server/lib/responseFormatter.js` - Output JSON-RPC format
- `server/lib/batchProcessor.js` - Handle batch requests
- `server/lib/rateLimiter.js` - Track and report rate limits
- `server/lib/traceContext.js` - Add distributed tracing

### Phase 3: Client Migration (Week 5-6)
1. Update C++ client to send JSON-RPC format
2. Add request ID generation
3. Implement response correlation
4. Handle notifications properly
5. **Implement server-initiated sync** (from API_IMPROVEMENTS.md)
6. **Add transaction support for multi-step operations** (from API_IMPROVEMENTS.md)

**Code Changes:**
- `src/core/input_handler.cpp` - Use JsonRpcClient instead of raw JSON
- `src/network/mcp_client.cpp` - Correlate responses by ID
- `src/core/state_manager.cpp` - Track pending requests
- `src/protocol/jsonrpc_client.cpp` - Implement strongly-typed client
- `src/protocol/transaction_manager.hpp` - Handle multi-step operations

### Phase 4: Deprecation (Week 7-8)
1. Add deprecation warnings for old format
2. Update documentation
3. Notify users of timeline
4. Remove old format support (after grace period)

## Backward Compatibility Strategy

During transition, support both formats:

```javascript
function parseMessage(message) {
  // JSON-RPC 2.0 format
  if (message.jsonrpc === "2.0") {
    return {
      format: 'jsonrpc',
      id: message.id,
      method: message.method,
      params: message.params,
      isNotification: !message.hasOwnProperty('id')
    };
  }
  
  // Legacy format
  if (message.type) {
    return {
      format: 'legacy',
      type: message.type,
      ...message
    };
  }
  
  throw new Error('Unknown message format');
}
```

## Example Conversations

### Simple Chat (JSON-RPC 2.0)
```
C→S: {"jsonrpc":"2.0","id":1,"method":"chat.send","params":{"content":"Hello"}}
S→C: {"jsonrpc":"2.0","id":1,"result":{"reply":"Hi there!","timestamp":123}}
```

### Chat with Tool Use
```
C→S: {"jsonrpc":"2.0","id":2,"method":"chat.send","params":{"content":"List files"}}
S→C: {"jsonrpc":"2.0","method":"tool.called","params":{"tool_name":"list_files"}}
S→C: {"jsonrpc":"2.0","method":"tool.result","params":{"preview":"file1.txt"}}
S→C: {"jsonrpc":"2.0","id":2,"result":{"reply":"I found file1.txt"}}
```

### Batch Request
```
C→S: [
  {"jsonrpc":"2.0","id":1,"method":"history.sync"},
  {"jsonrpc":"2.0","id":2,"method":"tools.list"}
]
S→C: [
  {"jsonrpc":"2.0","id":1,"result":{"entries":10}},
  {"jsonrpc":"2.0","id":2,"result":{"tools":["list_files"]}}
]
```

## Testing Strategy

### Unit Tests
- Parser handles both formats
- Request ID correlation works
- Error codes are correct
- Notifications have no ID

### Integration Tests
- Old clients work with new server
- New clients work with old server
- Batch requests process correctly
- Timeout handling preserves IDs

### Performance Tests
- No degradation vs current format
- Batch requests improve throughput
- ID tracking doesn't leak memory

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Breaking existing clients | High | Dual protocol support |
| Memory leak from ID tracking | Medium | Cleanup old IDs after timeout |
| Complex state management | Medium | Clear separation of concerns |
| User confusion | Low | Clear migration guide |

## Success Metrics

1. **Compatibility**: 100% of old clients still work
2. **Reliability**: Request correlation reduces errors by 50%
3. **Performance**: Batch requests improve throughput by 30%
4. **Adoption**: 80% of clients migrate within 3 months
5. **Support**: Reduced bug reports about desync issues

## Version Negotiation Protocol

**On Connection (from API_IMPROVEMENTS.md):**
```json
// Client Hello
{
  "jsonrpc": "2.0",
  "id": 0,
  "method": "rpc.hello",
  "params": {
    "version": "2.0",
    "clientVersion": "1.0.0",
    "capabilities": ["batch", "streaming", "tools", "transactions"]
  }
}

// Server Response
{
  "jsonrpc": "2.0",
  "id": 0,
  "result": {
    "version": "2.0",
    "serverVersion": "1.0.0",
    "capabilities": ["batch", "streaming", "tools", "kimi", "o3"],
    "sessionId": "abc-123-def",
    "rateLimit": {
      "limit": 100,
      "window": 3600
    }
  }
}
```

## Streaming Support Protocol

**For Long Responses (from API_IMPROVEMENTS.md):**
```json
// Initial response indicates streaming
{
  "jsonrpc": "2.0",
  "id": 42,
  "result": {
    "streaming": true,
    "streamId": "stream-123"
  }
}

// Stream chunks (notifications)
{
  "jsonrpc": "2.0",
  "method": "stream.chunk",
  "params": {
    "streamId": "stream-123",
    "chunk": "Here is the beginning...",
    "index": 0
  }
}

// Stream end
{
  "jsonrpc": "2.0",
  "method": "stream.end",
  "params": {
    "streamId": "stream-123",
    "totalChunks": 5
  }
}
```

## Rate Limiting Headers

**In Every Response (from API_IMPROVEMENTS.md):**
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "reply": "..."
  },
  "meta": {
    "rateLimit": {
      "limit": 100,
      "remaining": 87,
      "reset": 1234567890
    },
    "traceId": "abc-123-def",
    "spanId": "456"
  }
}
```

## Server-Initiated Sync

**Server Detects State Change (from API_IMPROVEMENTS.md):**
```json
{
  "jsonrpc": "2.0",
  "method": "sync.required",
  "params": {
    "reason": "file_changed",
    "action": "reload",
    "path": ".data/chat-history.json"
  }
}
```

## Transaction Support

**Multi-Step Operations (from API_IMPROVEMENTS.md):**
```json
// Begin transaction
{
  "jsonrpc": "2.0",
  "id": 100,
  "method": "transaction.begin",
  "params": {
    "operations": [
      "chat.send",
      "tools.execute",
      "history.save"
    ]
  }
}

// Execute steps with transaction ID
{
  "jsonrpc": "2.0",
  "id": 101,
  "method": "chat.send",
  "params": {
    "content": "Process this file",
    "transactionId": "txn-123"
  }
}

// Commit or rollback
{
  "jsonrpc": "2.0",
  "id": 110,
  "method": "transaction.commit",
  "params": {
    "transactionId": "txn-123"
  }
}
```

## Next Steps

1. [ ] Review and approve migration plan
2. [ ] Create strongly-typed C++ implementation
3. [ ] Set up feature flags for gradual rollout
4. [ ] Implement Phase 1 (dual protocol)
5. [ ] Test with subset of users
6. [ ] Full rollout with monitoring

## References

- [JSON-RPC 2.0 Specification](./JSON-RPC-2.0-Specification.md)
- [Current API Documentation](./MCP_BRIDGE_API.md)
- [API Improvement Analysis](./API_IMPROVEMENTS.md)