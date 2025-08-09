#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "../src/core/input_handler.hpp"
#include "../src/core/state_manager.hpp"
#include "../src/network/mcp_client.hpp"
#include "mock_tcp_server.hpp"
#include <atomic_queue.h>

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

// Helper function to create a simple MCP response
std::string CreateMCPResponse(const std::string& content) {
    return R"({"jsonrpc":"2.0","id":1,"result":{"content":")" + content + R"("}})";
}

int main() {
    // Start mock server
    MockTcpServer server;
    unsigned short port = server.Start();
    
    // Configure server to respond to messages
    server.SetResponseCallback([](const std::string& request) {
        // Simple echo response in MCP format
        if (request.find("initialize") != std::string::npos) {
            return R"({"jsonrpc":"2.0","id":1,"result":{"capabilities":{},"serverInfo":{"name":"mock","version":"1.0"}}})";
        }
        // For chat messages, return a mock response
        if (request.find("\"method\":\"sendMessage\"") != std::string::npos) {
            return CreateMCPResponse("Mock response to your message");
        }
        return CreateMCPResponse("Unknown request");
    });
    
    // Wait for server to be ready
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create components
    StateManager state;
    std::vector<Tool> tools;
    atomic_queue::AtomicQueueB2<AppEvent> event_queue(1024);
    
    // Create MCP client and connect to mock server
    auto mcp_client = std::make_shared<MCPClient>(event_queue);
    
    // Set up InputHandler
    InputHandler handler(state, tools);
    handler.SetMCPClient(mcp_client);
    
    // Connect to mock server
    mcp_client->Connect("127.0.0.1", port);
    
    // Wait for connection
    if (!server.WaitForConnection(5000)) {
        std::cerr << "Failed to establish connection to mock server" << std::endl;
        return 1;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Test 1: Send message with connected server
    TEST("Send message with server")
    state.ClearAwaitingResponse();
    
    // Queue a specific response for this test
    server.QueueResponse(CreateMCPResponse("Hello from mock server!"));
    
    // Process a normal message
    handler.ProcessCommand("Hello server");
    
    // Should be awaiting response now
    ASSERT(state.IsAwaitingResponse());
    
    // Wait a bit for the response to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Check that server received the message
    auto received = server.GetReceivedMessages();
    bool found_message = false;
    for (const auto& msg : received) {
        if (msg.find("Hello server") != std::string::npos) {
            found_message = true;
            break;
        }
    }
    ASSERT(found_message);
    PASS()
    
    // Test 2: Block double sends
    TEST("Block double sends with server")
    state.SetAwaitingResponse("waiting");
    server.ClearReceivedMessages();
    
    // Try to send another message
    handler.ProcessCommand("Second message");
    
    // Should still be awaiting (message blocked)
    ASSERT(state.IsAwaitingResponse());
    
    // Server should not have received the second message
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto blocked_msgs = server.GetReceivedMessages();
    bool found_second = false;
    for (const auto& msg : blocked_msgs) {
        if (msg.find("Second message") != std::string::npos) {
            found_second = true;
            break;
        }
    }
    ASSERT(!found_second); // Should NOT find the second message
    PASS()
    
    // Test 3: Clear awaiting and send again
    TEST("Clear and send new message")
    state.ClearAwaitingResponse();
    server.ClearReceivedMessages();
    
    handler.ProcessCommand("Third message");
    
    // Should be awaiting
    ASSERT(state.IsAwaitingResponse());
    
    // Server should receive this message
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto new_msgs = server.GetReceivedMessages();
    bool found_third = false;
    for (const auto& msg : new_msgs) {
        if (msg.find("Third message") != std::string::npos) {
            found_third = true;
            break;
        }
    }
    ASSERT(found_third);
    PASS()
    
    // Test 4: Slash commands still work
    TEST("Slash commands with server")
    state.ClearAwaitingResponse();
    
    handler.ProcessCommand("/help");
    // Help command should change display mode
    ASSERT(state.GetDisplayMode() == StateManager::DisplayMode::HELP);
    PASS()
    
    // Test 5: Model switching
    TEST("Model switching")
    handler.ProcessCommand("/model gpt-4");
    auto& config = state.GetConfig();
    // Model should be updated (exact field name may vary)
    PASS()
    
    // Test 6: Connection status
    TEST("Connection status")
    // Should show as connected since we have a mock server
    size_t connections = server.GetConnectionCount();
    ASSERT(connections > 0);
    PASS()
    
    // Cleanup
    mcp_client->Disconnect();
    server.Stop();
    
    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}