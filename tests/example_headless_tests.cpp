/**
 * Example test cases showing how to use the HeadlessApplication API
 * These would typically be written with a testing framework like Google Test or Catch2
 */

#include "../src/core/headless_application.hpp"
#include <cassert>
#include <iostream>

// Example 1: Basic command execution test
void test_basic_commands() {
    HeadlessApplication app;
    
    // Initialize without UI
    assert(app.Initialize("./config.json"));
    
    // Test help command
    auto result = app.ExecuteCommand("/help");
    assert(result.success);
    assert(result.response_type == LogEntryType::SYSTEM);
    assert(result.response.find("Available commands") != std::string::npos);
    
    // Test model selection
    result = app.ExecuteCommand("/model kimi");
    assert(result.success);
    assert(result.response.find("Model switched") != std::string::npos);
    
    // Verify model was changed
    auto [model, effort] = app.GetModelSettings();
    assert(model == "kimi");
    
    std::cout << "✓ Basic commands test passed\n";
}

// Example 2: Chat message with server connection
void test_chat_with_server() {
    HeadlessApplication app;
    
    app.Initialize("./config.json");
    
    // Connect to server
    assert(app.Connect("127.0.0.1", 4000));
    
    // Register callback to capture responses
    std::vector<std::string> responses;
    app.OnMessage([&responses](LogEntryType type, const std::string& content) {
        if (type == LogEntryType::RESPONSE) {
            responses.push_back(content);
        }
    });
    
    // Send chat message
    auto result = app.SendChatMessage("Hello, how are you?");
    assert(result.success);
    assert(result.response_type == LogEntryType::RESPONSE);
    assert(!result.response.empty());
    
    // Verify token count increased
    assert(app.GetTokenCount() > 0);
    
    std::cout << "✓ Chat with server test passed\n";
}

// Example 3: Async command execution
void test_async_execution() {
    HeadlessApplication app;
    app.Initialize("./config.json");
    
    bool callback_executed = false;
    std::string async_response;
    
    // Execute command asynchronously
    app.ExecuteCommandAsync("/tools", [&](bool success, const std::string& result) {
        callback_executed = true;
        async_response = result;
    });
    
    // Wait for callback
    app.WaitForState([&]() { return callback_executed; });
    
    assert(callback_executed);
    assert(!async_response.empty());
    
    std::cout << "✓ Async execution test passed\n";
}

// Example 4: Conversation history management
void test_conversation_history() {
    HeadlessApplication app;
    app.Initialize("./config.json");
    app.Connect();
    
    // Send some messages to build history
    app.SendChatMessage("First message");
    app.SendChatMessage("Second message");
    
    // Get conversation log
    auto log = app.GetConversationLog();
    assert(log.size() >= 4); // At least 2 user + 2 response entries
    
    // Clear and verify
    app.ClearConversation();
    log = app.GetConversationLog();
    assert(log.size() == 1); // Should have welcome message only
    
    // Test loading saved conversations
    auto histories = app.GetChatHistories();
    if (!histories.empty()) {
        assert(app.LoadChatHistory(1));
        log = app.GetConversationLog();
        assert(!log.empty());
    }
    
    std::cout << "✓ Conversation history test passed\n";
}

// Example 5: Timeout and error handling
void test_timeout_handling() {
    HeadlessApplication app;
    app.Initialize("./config.json");
    
    // Simulate timeout scenario
    app.SimulateTimeout();
    
    // Try to send message - should timeout
    auto result = app.SendChatMessage("Test message", std::chrono::milliseconds(1000));
    assert(!result.success || result.response_type == LogEntryType::ERROR);
    
    // Verify state
    assert(!app.IsAwaitingResponse());
    
    std::cout << "✓ Timeout handling test passed\n";
}

// Example 6: State monitoring
void test_state_monitoring() {
    HeadlessApplication app;
    app.Initialize("./config.json");
    
    std::vector<std::string> state_changes;
    
    // Monitor state changes
    app.OnStateChange([&state_changes](const std::string& type, const std::string& data) {
        state_changes.push_back(type + ": " + data);
        std::cout << "State change: " << type << " -> " << data << "\n";
    });
    
    // Trigger various state changes
    app.SetModel("o3");
    app.SetReasoningEffort("high");
    app.ExecuteCommand("/new");
    
    // Verify state changes were captured
    assert(!state_changes.empty());
    
    std::cout << "✓ State monitoring test passed\n";
}

// Example 7: Batch command execution
void test_batch_commands() {
    HeadlessApplication app;
    app.Initialize("./config.json");
    
    // Execute multiple commands in sequence
    std::vector<std::string> commands = {
        "/model o3",
        "/think high",
        "/clear",
        "/sync"
    };
    
    for (const auto& cmd : commands) {
        auto result = app.ExecuteCommand(cmd);
        assert(result.success);
        std::cout << "Executed: " << cmd << " (took " 
                  << result.elapsed_time.count() << "ms)\n";
    }
    
    std::cout << "✓ Batch commands test passed\n";
}

// Example 8: Connection resilience
void test_connection_resilience() {
    HeadlessApplication app;
    app.Initialize("./config.json");
    
    bool disconnected = false;
    bool reconnected = false;
    
    app.OnConnectionChange([&](bool connected) {
        if (!connected && !disconnected) {
            disconnected = true;
        } else if (connected && disconnected) {
            reconnected = true;
        }
    });
    
    // Connect initially
    assert(app.Connect());
    
    // Simulate disconnect
    app.SimulateDisconnect();
    
    // Wait for disconnect notification
    app.WaitForState([&]() { return disconnected; });
    
    // Try to reconnect
    assert(app.Connect());
    
    // Wait for reconnect notification
    app.WaitForState([&]() { return reconnected; });
    
    assert(disconnected && reconnected);
    
    std::cout << "✓ Connection resilience test passed\n";
}

// Main test runner
int main() {
    std::cout << "Running headless application tests...\n\n";
    
    try {
        test_basic_commands();
        test_chat_with_server();
        test_async_execution();
        test_conversation_history();
        test_timeout_handling();
        test_state_monitoring();
        test_batch_commands();
        test_connection_resilience();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}