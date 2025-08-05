# Chat History Design Documentation

## Overview

The chat history system uses a **dual-source architecture** where the C++ client is the authoritative source for persistent storage, while the Node.js server maintains an in-memory working set for API interactions.

## Architecture Components

### 1. **C++ Client (Authoritative Source)**
- **File**: `chat-history.json` (NDJSON format)
- **Purpose**: Persistent storage of conversation context for LLM API
- **Behavior**: Stream-based append-only writing
- **Token Tracking**: Calculates and stores token counts using tiktoken-compatible estimator
- **Lifecycle Management**: Handles file rotation on `/new` commands

### 2. **Node.js Server (Working Memory)**
- **Storage**: In-memory `chatHistory` array
- **Purpose**: Provides conversation context to LLM API calls  
- **Behavior**: Loads from file on startup, maintains working set during session
- **Sync**: Updates when processing new messages, clears on `/new`

### 3. **C++ UI Conversation Log**
- **Storage**: `std::deque<LogEntry>` in memory
- **Purpose**: Complete UI conversation display (includes system messages, errors)
- **Scope**: Broader than chat history (shows all UI interactions)
- **Persistence**: Separate `.data/conversation.log` for debugging/audit

## Data Flow

### Message Processing Flow
```
User Input → C++ Client → Node.js Server → LLM API
     ↓             ↓              ↓            ↓
UI Display   Write to File   Update Memory   Response
     ↑             ↑              ↑            ↓
Response Display ← Write to File ← Update Memory ← Process
     ↑
C++ UI Token Count Update
```

### Startup Sequence
1. **C++ Client** loads `chat-history.json` to restore:
   - Conversation display in UI
   - Accurate token count for context size
2. **Node.js Server** loads `chat-history.json` to restore:
   - In-memory conversation context for API calls

## File Formats

### chat-history.json (NDJSON)
```json
{"role": "user", "content": "Hello", "token_cnt": 3, "timestamp": "2025-08-05T10:14:23.456Z"}
{"role": "assistant", "content": "Hi there!", "token_cnt": 4, "timestamp": "2025-08-05T10:14:25.789Z"}
```

### conversation.log (NDJSON) 
```json
{"type": "USER", "content": "Hello", "timestamp": "2025-08-05T10:14:23.456Z"}
{"type": "SYSTEM", "content": "[Awaiting response...]", "timestamp": "2025-08-05T10:14:23.457Z"}
{"type": "RESPONSE", "content": "Hi there!", "timestamp": "2025-08-05T10:14:25.789Z"}
```

## Key Differences: Chat History vs Conversation Log

| Aspect | chat-history.json | conversation.log |
|--------|------------------|------------------|
| **Purpose** | LLM API context | Full UI audit trail |
| **Content** | user/assistant only | All message types |
| **Token Info** | ✓ Included | ✗ Not included |
| **System Messages** | ✗ Excluded | ✓ Included |
| **Error Messages** | ✗ Excluded | ✓ Included |
| **Restoration** | ✓ Loads on startup | ✗ Append-only log |

## Commands

### `/new` - Start New Conversation
**C++ Client Actions:**
1. Rotate `chat-history.json` → `chat-history-{timestamp}.json`
2. Reset token counter to 0
3. Clear UI conversation display
4. Send `/new` to server

**Node.js Server Actions:**
1. Clear in-memory `chatHistory` array
2. Send confirmation to client

### `/sync` - Check Synchronization  
**Process:**
1. **C++ Client** requests sync check from server
2. **Node.js Server** responds with in-memory history stats
3. **C++ Client** compares with file stats and reports:
   - ✓ "Synchronized - Counts match" 
   - ⚠ "Out of sync - Entry counts differ"

**Sync Check Covers:**
- Entry count comparison between file and server memory
- Basic validation that both sources have same conversation length

## Token Counting

### Implementation
- **Library**: `token-estimator` (tiktoken-compatible, ~10μs per 1k chars)
- **Calculation**: Done by C++ client when writing messages
- **Storage**: Stored in `chat-history.json` as `token_cnt` field
- **Display**: Real-time "Context Size: N tokens" in UI header
- **Atomic Operations**: Thread-safe counter using `std::atomic<size_t>`

### Accuracy
- ~1-3% accurate compared to Python tiktoken
- Sufficient for context window management and cost estimation
- Uses cl100k_base vocabulary (GPT-3.5/GPT-4 compatible)

## Error Scenarios & Recovery

### Out of Sync Detection
**Causes:**
- Server restart without file reload
- File corruption or partial writes
- Race conditions during concurrent access

**Recovery:**
- Manual: Restart server to reload from file
- Automatic: Server reloads file on next startup

### File Rotation Issues
**Protection:**
- Atomic file operations using `std::filesystem::rename`
- Error handling for permission/disk space issues
- Backup preservation with timestamp naming

## Performance Characteristics

### File Operations
- **Append-only writes**: O(1) for new messages
- **Startup loading**: O(n) where n = message count, ~2GB/s with simdjson
- **Token calculation**: ~10μs per 1k characters
- **File rotation**: O(1) atomic rename operation

### Memory Usage
- **C++ UI log**: Bounded by `max_log_size` (default: 10,000 entries)
- **Node.js memory**: Unbounded growth until `/new` (typical: <100 entries)
- **File size**: ~100-200 bytes per message entry

## Configuration

### Token Limits
- **Default context tracking**: Unlimited (user manages via UI display)
- **File rotation**: Manual via `/new` command
- **UI conversation**: Auto-truncated at `max_log_size`

### File Locations
- **Chat history**: `.data/chat-history.json`
- **Conversation log**: `.data/conversation.log`  
- **Command history**: `.data/history.log`
- **Backups**: `.data/chat-history-{timestamp}.json`

## Thread Safety

### C++ Client
- **Token counter**: `std::atomic<size_t>` for thread-safe updates
- **File writes**: Single-threaded through main event loop
- **UI updates**: Event-driven with atomic screen refreshes

### Node.js Server  
- **Single-threaded**: Node.js event loop handles all operations
- **File operations**: Synchronous to prevent race conditions
- **Memory updates**: Sequential processing of client messages

## Future Considerations

### Potential Improvements
1. **Incremental sync**: Compare message checksums, not just counts
2. **Auto-recovery**: Server auto-reload from file on sync failures  
3. **Compression**: Large conversation archives with gzip
4. **Chunking**: Automatic context window management with sliding windows
5. **Backup management**: Automatic cleanup of old backup files

### Known Limitations
1. **Basic sync checking**: Only compares entry counts, not content
2. **No conflict resolution**: Manual intervention required for sync issues
3. **Memory growth**: Server memory grows unbounded until `/new`
4. **Single session**: No multi-user or concurrent session support