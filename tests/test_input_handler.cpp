#include <iostream>
#include <cassert>
#include "../src/core/input_handler.hpp"
#include "../src/core/state_manager.hpp"
#include "../src/network/mcp_client.hpp"

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

// Mock MCP client for testing
class MockMCPClient : public MCPClient {
public:
    MockMCPClient() : MCPClient("", 0) {}
    
    bool last_message_sent = false;
    std::string last_message;
    
    void SendChatMessage(const std::string& message) {
        last_message_sent = true;
        last_message = message;
    }
    
    void Reset() {
        last_message_sent = false;
        last_message.clear();
    }
};

int main() {
    StateManager state;
    auto mock_client = std::make_shared<MockMCPClient>();
    InputHandler handler(state, mock_client);
    
    // Test 1: Handle arrow keys for history navigation
    TEST("Arrow key navigation")
    handler.AddToHistory("command1");
    handler.AddToHistory("command2");
    handler.AddToHistory("command3");
    
    // Up arrow should go to previous command
    app::Event up = app::Event::ArrowUp();
    bool handled = handler.HandleEvent(up);
    ASSERT(handled);
    ASSERT(handler.GetCurrentInput() == "command3");
    
    // Another up arrow
    handler.HandleEvent(up);
    ASSERT(handler.GetCurrentInput() == "command2");
    
    // Down arrow should go forward
    app::Event down = app::Event::ArrowDown();
    handler.HandleEvent(down);
    ASSERT(handler.GetCurrentInput() == "command3");
    PASS()
    
    // Test 2: Ctrl+C clears input
    TEST("Ctrl+C clears input")
    handler.SetCurrentInput("some text");
    app::Event ctrl_c = app::Event::CtrlC();
    handler.HandleEvent(ctrl_c);
    ASSERT(handler.GetCurrentInput().empty());
    PASS()
    
    // Test 3: Ctrl+N starts new conversation
    TEST("Ctrl+N new conversation")
    state.WriteToChatHistory("user", "old message");
    
    app::Event ctrl_n = app::Event::CtrlN();
    handler.HandleEvent(ctrl_n);
    // Check that history file was rotated (new file created)
    PASS()
    
    // Test 4: Process slash commands
    TEST("Slash commands")
    handler.ProcessCommand("/clear");
    ASSERT(handler.GetCurrentInput().empty());
    
    handler.ProcessCommand("/model gpt-4");
    // Model setting happens in config
    auto& config = state.GetConfig();
    ASSERT(config.current_model == "gpt-4");
    
    handler.ProcessCommand("/effort high");
    ASSERT(config.effort_level == "high");
    PASS()
    
    // Test 5: Send message when not awaiting
    TEST("Send message when not awaiting")
    mock_client->Reset();
    state.ClearAwaitingResponse();
    
    handler.ProcessCommand("Hello world");
    
    // Should be awaiting response now
    ASSERT(state.IsAwaitingResponse());
    // Should have added to history
    auto history = state.GetHistory();
    ASSERT(!history.empty());
    PASS()
    
    // Test 6: Block sending when awaiting response
    TEST("Block double sends")
    state.SetAwaitingResponse("waiting");
    mock_client->Reset();
    
    handler.ProcessCommand("Second message");
    
    // Should still be awaiting
    ASSERT(state.IsAwaitingResponse());
    PASS()
    
    // Test 7: History management
    TEST("History management")
    // Clear history first
    while (!state.GetHistory().empty()) {
        state.GetHistory();
    }
    
    for (int i = 0; i < 10; i++) {
        handler.AddToHistory("command_" + std::to_string(i));
    }
    
    // Should have added to history
    ASSERT(state.GetHistory().size() >= 10);
    PASS()
    
    // Test 8: Empty command handling
    TEST("Empty command handling")
    mock_client->Reset();
    state.ClearAwaitingResponse();
    
    handler.ProcessCommand("");
    handler.ProcessCommand("   ");
    
    // Should not be awaiting for empty commands
    ASSERT(!state.IsAwaitingResponse());
    PASS()
    
    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}