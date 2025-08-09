# Headless API Design for Non-UI Testing

## Overview

The headless API provides a way to test all non-UI logic of the application without FTXUI dependencies. This enables:
- Unit and integration testing
- Automated testing in CI/CD pipelines
- Scripting and automation
- Performance testing
- API testing without UI overhead

## Architecture Analysis

### Current UI Dependencies

| Component | FTXUI Usage | UI-Agnostic? |
|-----------|-------------|--------------|
| **StateManager** | None | ✅ Yes |
| **MCPClient** | None | ✅ Yes |
| **InputHandler** | Event type only | ✅ Mostly |
| **UIRenderer** | Heavy (DOM elements) | ❌ No |
| **InputWithHistory** | ComponentBase inheritance | ❌ No |
| **ConversationLogManager** | Event type only | ✅ Mostly |
| **Application** | Heavy (Screen, Components) | ❌ No |

### Message Flow
```
User Input → InputHandler → StateManager/MCPClient → Server
                    ↓              ↓
                UI Updates    Chat History
```

## Proposed Headless API

### Core Features

1. **Command Execution**
   - Synchronous execution with timeout
   - Asynchronous execution with callbacks
   - Batch command processing
   - Full command set support (chat, /commands)

2. **Event System**
   - Message callbacks (user, system, response, error)
   - State change notifications
   - Connection status monitoring
   - Custom event handlers

3. **State Management**
   - Full access to StateManager
   - Conversation history management
   - Token counting
   - Model/settings configuration

4. **Testing Utilities**
   - Network simulation (disconnect/reconnect)
   - Timeout simulation
   - State predicates with wait
   - Event processing control

### API Methods

#### Initialization
```cpp
HeadlessApplication app;
app.Initialize("config.json");
app.Connect("127.0.0.1", 4000);
app.Start();
```

#### Command Execution
```cpp
// Synchronous
auto result = app.ExecuteCommand("/help");
assert(result.success);

// Asynchronous
app.ExecuteCommandAsync("/tools", [](bool success, const std::string& result) {
    // Handle result
});

// Chat message
auto response = app.SendChatMessage("Hello!");
```

#### Event Handling
```cpp
// Monitor messages
app.OnMessage([](LogEntryType type, const std::string& content) {
    std::cout << "Message: " << content << std::endl;
});

// Monitor state changes
app.OnStateChange([](const std::string& type, const std::string& data) {
    std::cout << "State change: " << type << std::endl;
});
```

#### State Access
```cpp
// Get conversation
auto log = app.GetConversationLog();

// Check tokens
size_t tokens = app.GetTokenCount();

// Load history
app.LoadChatHistory(1);

// Clear conversation
app.ClearConversation();
```

## Implementation Strategy

### Phase 1: Core Extraction
1. ✅ Extract StateManager (complete)
2. ✅ Extract InputHandler (complete)
3. ✅ Extract MCPClient (complete)
4. Create HeadlessApplication wrapper

### Phase 2: Interface Design
1. Define minimal event system
2. Create command result structures
3. Design callback interfaces
4. Implement state access methods

### Phase 3: Testing Framework
1. Create test fixtures
2. Write unit tests for each component
3. Create integration tests
4. Add performance benchmarks

### Phase 4: Refactoring
1. Remove unnecessary FTXUI dependencies
2. Create abstraction layer for Event type
3. Separate UI-specific logic from business logic
4. Create factory pattern for UI/Headless modes

## Benefits

1. **Testing**
   - Fast unit tests without UI overhead
   - Deterministic testing (no UI timing issues)
   - Easy mocking and stubbing
   - CI/CD integration

2. **Automation**
   - Scriptable interface
   - Batch processing
   - Performance testing
   - Load testing

3. **Development**
   - Faster iteration on business logic
   - Better separation of concerns
   - Easier debugging
   - Parallel development (UI and logic)

## Usage Examples

### Unit Test Example
```cpp
TEST(ChatTest, SendMessage) {
    HeadlessApplication app;
    app.Initialize("test-config.json");
    app.Connect();
    
    auto result = app.SendChatMessage("Test message");
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.response_type, LogEntryType::RESPONSE);
    EXPECT_GT(app.GetTokenCount(), 0);
}
```

### Integration Test Example
```cpp
TEST(IntegrationTest, FullConversation) {
    HeadlessApplication app;
    app.Initialize("config.json");
    app.Connect();
    
    // Send multiple messages
    app.SendChatMessage("Hello");
    app.SendChatMessage("Tell me a joke");
    
    // Verify conversation
    auto log = app.GetConversationLog();
    EXPECT_GE(log.size(), 4);
    
    // Save and reload
    app.ExecuteCommand("/new");
    EXPECT_EQ(app.GetConversationLog().size(), 1);
}
```

### Performance Test Example
```cpp
TEST(PerformanceTest, ResponseTime) {
    HeadlessApplication app;
    app.Initialize("config.json");
    app.Connect();
    
    auto start = std::chrono::steady_clock::now();
    auto result = app.SendChatMessage("Quick response test");
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    EXPECT_LT(elapsed, std::chrono::seconds(5));
    EXPECT_TRUE(result.success);
}
```

## Next Steps

1. **Implement HeadlessApplication class**
   - Start with stub implementation
   - Add core functionality incrementally
   - Write tests alongside implementation

2. **Create abstraction layer**
   - Abstract Event type to remove FTXUI dependency
   - Create interface for UI/Headless modes
   - Implement factory pattern

3. **Write comprehensive tests**
   - Unit tests for each component
   - Integration tests for workflows
   - Performance benchmarks

4. **Documentation**
   - API documentation
   - Testing guide
   - Migration guide for existing code

## Conclusion

The headless API design provides a clean separation between UI and business logic, enabling comprehensive testing and automation. By extracting core components and creating a well-defined interface, we can test all non-UI functionality independently while maintaining the same behavior as the full application.