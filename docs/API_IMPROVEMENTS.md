# MCP Bridge API - Improvement Recommendations

## Current Issues

### 1. Inconsistent Message Format
- **Problem**: Not using standard JSON-RPC 2.0 format
- **Current**: Custom `type` field with various payload structures
- **Impact**: Harder to validate, no request/response correlation

**Recommendation**: Migrate to JSON-RPC 2.0:
```json
// Current
{"type": "chat", "content": "Hello"}

// Improved
{"jsonrpc": "2.0", "id": 1, "method": "chat", "params": {"content": "Hello"}}
```

### 2. No Request ID Correlation
- **Problem**: Can't match responses to specific requests
- **Current**: Fire-and-forget pattern, responses have no request ID
- **Impact**: Difficult to handle concurrent requests or implement proper error recovery

### 3. Inconsistent Error Handling
- **Problem**: Multiple error formats (`error`, `timeout_error`)
- **Current**: Different structures for different error types
- **Impact**: Complex client-side error handling

**Recommendation**: Standardize error format:
```json
{
  "type": "error",
  "error": {
    "code": -32000,  // Standard error code
    "message": "Request timeout",
    "data": {
      "timeout": 120000,
      "canRetry": true,
      "originalMessage": "..."
    }
  }
}
```

### 4. Missing API Versioning
- **Problem**: No version negotiation or compatibility checking
- **Current**: Implicit version compatibility
- **Impact**: Breaking changes are hard to manage

**Recommendation**: Add version negotiation on connect:
```json
// Client
{"type": "hello", "version": "1.0", "capabilities": ["tools", "streaming"]}

// Server
{"type": "hello", "version": "1.0", "capabilities": ["tools", "kimi", "o3"]}
```

### 5. No Streaming Support
- **Problem**: Long responses block until complete
- **Current**: Single response message after full generation
- **Impact**: Poor UX for long responses

**Recommendation**: Add streaming messages:
```json
{"type": "response_chunk", "id": 123, "chunk": "Here is ", "done": false}
{"type": "response_chunk", "id": 123, "chunk": "the answer.", "done": true}
```

### 6. Weak Type Safety
- **Problem**: No schema validation
- **Current**: Manual type checking in code
- **Impact**: Runtime errors, inconsistent handling

**Recommendation**: 
- Define TypeScript interfaces
- Use JSON Schema validation
- Generate types from OpenAPI spec

### 7. No Rate Limiting Info
- **Problem**: Client doesn't know API limits
- **Current**: Errors occur without warning
- **Impact**: Poor error recovery

**Recommendation**: Include rate limit headers:
```json
{
  "type": "response",
  "reply": "...",
  "metadata": {
    "rateLimit": {
      "limit": 100,
      "remaining": 87,
      "reset": 1234567890
    }
  }
}
```

### 8. Unclear State Synchronization
- **Problem**: Manual sync required after certain operations
- **Current**: Client must remember to send `reload` after `/load`
- **Impact**: Desynchronization bugs

**Recommendation**: Server-initiated sync:
```json
// Server detects file change
{"type": "sync_required", "reason": "file_changed", "action": "reload"}
```

### 9. No Transaction Support
- **Problem**: Multi-step operations aren't atomic
- **Current**: Each message is independent
- **Impact**: Partial state on failures

### 10. Limited Monitoring/Debugging
- **Problem**: No built-in request tracing
- **Current**: Manual log correlation
- **Impact**: Hard to debug production issues

**Recommendation**: Add trace IDs:
```json
{
  "type": "chat",
  "content": "Hello",
  "metadata": {
    "traceId": "abc-123-def",
    "spanId": "456"
  }
}
```

## Migration Strategy

### Phase 1: Documentation (Complete)
✅ Document current API
✅ Identify improvement areas

### Phase 2: Backward Compatible Additions
- Add optional `id` field to requests
- Add optional `metadata` to responses
- Implement version negotiation

### Phase 3: Dual Protocol Support
- Support both old and new message formats
- Deprecation warnings for old format
- Client capability detection

### Phase 4: Full Migration
- Remove old format support
- Require JSON-RPC 2.0
- Implement all improvements

## Priority Improvements

1. **High Priority**
   - Add request IDs for correlation
   - Standardize error format
   - Add version negotiation

2. **Medium Priority**
   - Implement streaming responses
   - Add rate limit information
   - Improve type safety

3. **Low Priority**
   - Add transaction support
   - Implement tracing
   - Server-initiated sync

## Testing Requirements

- Unit tests for each message type
- Integration tests for workflows
- Backward compatibility tests
- Error recovery scenarios
- Performance benchmarks