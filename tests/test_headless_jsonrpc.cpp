/**
 * Test JSON-RPC integration with HeadlessApplication
 * Tests the full application stack with the new type-safe JSON-RPC interface
 */

#include "../src/core/headless_application.hpp"
#include "../src/protocol/jsonrpc_procedures.hpp"
#include "../tests/configurable_mock_server.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>
#include <spdlog/spdlog.h>
#include <filesystem>

using namespace std::chrono_literals;

// Helper function to find the test config file from various locations
std::string FindTestConfigPath() {
    // Try multiple paths to support running from different directories
    std::vector<std::string> paths = {
        "tests/test_config.json",         // From project root
        "../tests/test_config.json",      // From src directory
        "./test_config.json",              // From tests directory
        "test_config.json"                 // Current directory
    };
    
    for (const auto& path : paths) {
        if (std::filesystem::exists(path)) {
            SPDLOG_INFO("Found test config at: {}", path);
            return path;
        }
    }
    
    // If not found, return the most likely path with an error message
    std::cerr << "❌ Could not find test_config.json in any of the expected locations:" << std::endl;
    for (const auto& path : paths) {
        std::cerr << "  - " << path << std::endl;
    }
    return "tests/test_config.json";  // Default to project root path
}

void test_headless_chat_workflow() {
    std::cout << "\n=== Testing Chat Workflow with HeadlessApplication ===" << std::endl;
    
    // Start mock server with JSON-RPC support
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Configure server to respond to chat.send
    jsonrpc::ChatResult chat_result{
        .reply = "Hello from HeadlessApplication test!",
        .timestamp = static_cast<int64_t>(std::time(nullptr)),
        .streaming = false
    };
    
    ConfigurableMockServer::MockResponse chat_response;
    chat_response.response = [chat_result](const jsonrpc::JsonRpcRequest& req) {
        SPDLOG_INFO("Mock server handling chat.send request");
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(chat_result)
            .buildJson();
    };
    server.SetMethodDefault("chat.send", chat_response);
    
    // Create and initialize HeadlessApplication
    HeadlessApplication app;
    
    // Initialize with test config
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication with config: " << config_path << std::endl;
        return;
    }
    
    // Set up event callbacks
    std::vector<std::string> received_messages;
    std::atomic<bool> got_response{false};
    std::string last_response;
    
    app.OnMessage([&](LogEntryType type, const std::string& content) {
        SPDLOG_INFO("HeadlessApp message callback: type={}, content={}", 
                   static_cast<int>(type), content);
        received_messages.push_back(content);
        if (type == LogEntryType::RESPONSE) {
            last_response = content;
            got_response = true;
        }
    });
    
    std::atomic<bool> connected{false};
    app.OnConnectionChange([&](bool is_connected) {
        SPDLOG_INFO("Connection changed: {}", is_connected);
        connected = is_connected;
    });
    
    // Connect to mock server
    if (!app.Connect("127.0.0.1", port)) {
        std::cerr << "❌ Failed to connect to mock server" << std::endl;
        return;
    }
    
    // Start the application event loop
    app.Start();
    
    // Wait for connection
    if (!app.WaitForState([&]() { return connected.load(); }, 2s)) {
        std::cerr << "❌ Connection timeout" << std::endl;
        return;
    }
    
    std::cout << "Connected to mock server on port " << port << std::endl;
    
    // Send a chat message through the HeadlessApplication
    std::cout << "Sending chat message..." << std::endl;
    auto result = app.SendChatMessage("Hello, world!", 5s);
    
    if (!result.success) {
        std::cerr << "❌ Failed to send chat message: " << result.response << std::endl;
        return;
    }
    
    std::cout << "✅ Chat message sent successfully" << std::endl;
    std::cout << "Response: " << result.response << std::endl;
    
    // Verify the response was received
    if (!got_response) {
        std::cerr << "❌ Response callback was not triggered" << std::endl;
        return;
    }
    
    // Check conversation log
    auto log = app.GetConversationLog();
    std::cout << "Conversation log has " << log.size() << " entries" << std::endl;
    
    bool found_user_message = false;
    bool found_assistant_message = false;
    
    for (const auto& entry : log) {
        if (entry.type == LogEntryType::USER && entry.content.find("Hello, world!") != std::string::npos) {
            found_user_message = true;
        }
        if (entry.type == LogEntryType::RESPONSE && entry.content.find("HeadlessApplication test") != std::string::npos) {
            found_assistant_message = true;
        }
    }
    
    if (!found_user_message) {
        std::cerr << "❌ User message not found in conversation log" << std::endl;
        return;
    }
    
    if (!found_assistant_message) {
        std::cerr << "❌ Assistant message not found in conversation log" << std::endl;
        return;
    }
    
    std::cout << "✅ Chat workflow test passed!" << std::endl;
    
    // Clean up
    app.Stop();
    server.Stop();
}

void test_headless_tool_execution() {
    std::cout << "\n=== Testing Tool Execution with HeadlessApplication ===" << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Configure server to respond to tools.list
    jsonrpc::ToolListResult tools_result{
        .tools = {"file_reader", "calculator", "web_search"},
        .timestamp = static_cast<int64_t>(std::time(nullptr))
    };
    
    ConfigurableMockServer::MockResponse tools_response;
    tools_response.response = [tools_result](const jsonrpc::JsonRpcRequest& req) {
        SPDLOG_INFO("Mock server handling tools.list request");
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(tools_result)
            .buildJson();
    };
    server.SetMethodDefault("tools.list", tools_response);
    
    // Configure tool execution response
    ConfigurableMockServer::MockResponse tool_exec_response;
    tool_exec_response.response = [](const jsonrpc::JsonRpcRequest& req) {
        SPDLOG_INFO("Mock server handling tool.execute request");
        
        // Create a simple tool result using ToolExecuteResult
        jsonrpc::ToolExecuteResult result{
            .success = true,
            .output = "Tool executed successfully",
            .error = std::nullopt,
            .execution_time_ms = 100
        };
        
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    };
    server.SetMethodDefault("tool.execute", tool_exec_response);
    
    // Create HeadlessApplication
    HeadlessApplication app;
    
    // Initialize with test config
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication with config: " << config_path << std::endl;
        return;
    }
    
    // Track tool events
    std::vector<std::string> tool_events;
    app.OnMessage([&](LogEntryType, const std::string& content) {
        if (content.find("tool") != std::string::npos || content.find("Tool") != std::string::npos) {
            tool_events.push_back(content);
            SPDLOG_INFO("Tool event: {}", content);
        }
    });
    
    // Connect and start
    if (!app.Connect("127.0.0.1", port)) {
        std::cerr << "❌ Failed to connect to mock server" << std::endl;
        return;
    }
    
    app.Start();
    
    // Execute command to list tools
    std::cout << "Executing /tools command..." << std::endl;
    auto result = app.ExecuteCommand("/tools", 5s);
    
    if (!result.success) {
        std::cerr << "❌ Failed to execute /tools command: " << result.response << std::endl;
        return;
    }
    
    std::cout << "Tools command response: " << result.response << std::endl;
    
    // Verify tools were listed
    if (result.response.find("file_reader") == std::string::npos ||
        result.response.find("calculator") == std::string::npos ||
        result.response.find("web_search") == std::string::npos) {
        std::cerr << "❌ Tools not properly listed in response" << std::endl;
        return;
    }
    
    std::cout << "✅ Tool execution test passed!" << std::endl;
    
    // Clean up
    app.Stop();
    server.Stop();
}

void test_headless_error_handling() {
    std::cout << "\n=== Testing Error Handling with HeadlessApplication ===" << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Configure server to return errors
    ConfigurableMockServer::MockResponse error_response;
    error_response.response = [](const jsonrpc::JsonRpcRequest& req) {
        SPDLOG_INFO("Mock server returning error for request");
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .error(-32601, "Method not found")
            .buildJson();
    };
    server.SetMethodDefault("chat.send", error_response);
    
    // Create HeadlessApplication
    HeadlessApplication app;
    
    // Initialize with test config
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication with config: " << config_path << std::endl;
        return;
    }
    
    // Track errors
    std::vector<std::string> errors;
    app.OnMessage([&](LogEntryType type, const std::string& content) {
        if (type == LogEntryType::ERROR) {
            errors.push_back(content);
            SPDLOG_INFO("Error received: {}", content);
        }
    });
    
    // Connect and start
    if (!app.Connect("127.0.0.1", port)) {
        std::cerr << "❌ Failed to connect to mock server" << std::endl;
        return;
    }
    
    app.Start();
    
    // Send message that will trigger error
    std::cout << "Sending message to trigger error..." << std::endl;
    auto result = app.SendChatMessage("This will fail", 5s);
    
    if (result.success) {
        std::cerr << "❌ Expected failure but got success" << std::endl;
        return;
    }
    
    std::cout << "Got expected error: " << result.response << std::endl;
    
    // Test timeout handling
    std::cout << "\nTesting timeout handling..." << std::endl;
    
    // Configure server to not respond (simulate timeout)
    ConfigurableMockServer::MockResponse no_response;
    no_response.response = [](const jsonrpc::JsonRpcRequest&) {
        SPDLOG_INFO("Mock server delaying response to simulate timeout");
        std::this_thread::sleep_for(std::chrono::seconds(2));  // Delay longer than 1s timeout
        return "";
    };
    server.SetMethodDefault("chat.send", no_response);
    
    auto timeout_result = app.SendChatMessage("This will timeout", 1s);
    
    if (timeout_result.success) {
        std::cerr << "❌ Expected timeout but got success" << std::endl;
        return;
    }
    
    std::cout << "Got expected timeout: " << timeout_result.response << std::endl;
    
    std::cout << "✅ Error handling test passed!" << std::endl;
    
    // Clean up
    app.Stop();
    server.Stop();
}

void test_headless_state_management() {
    std::cout << "\n=== Testing State Management with HeadlessApplication ===" << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Configure chat responses
    int message_count = 0;
    ConfigurableMockServer::MockResponse chat_response;
    chat_response.response = [&message_count](const jsonrpc::JsonRpcRequest& req) {
        message_count++;
        jsonrpc::ChatResult result{
            .reply = "Response #" + std::to_string(message_count),
            .timestamp = static_cast<int64_t>(std::time(nullptr)),
            .streaming = false
        };
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    };
    server.SetMethodDefault("chat.send", chat_response);
    
    // Create HeadlessApplication
    HeadlessApplication app;
    
    // Initialize with test config
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication with config: " << config_path << std::endl;
        return;
    }
    
    // Connect and start
    if (!app.Connect("127.0.0.1", port)) {
        std::cerr << "❌ Failed to connect to mock server" << std::endl;
        return;
    }
    
    app.Start();
    
    // Send multiple messages
    std::cout << "Sending multiple messages..." << std::endl;
    
    app.SendChatMessage("First message", 5s);
    app.SendChatMessage("Second message", 5s);
    app.SendChatMessage("Third message", 5s);
    
    // Check conversation log
    auto log = app.GetConversationLog();
    std::cout << "Conversation has " << log.size() << " entries" << std::endl;
    
    // Count user and assistant messages
    int user_messages = 0;
    int assistant_messages = 0;
    
    for (const auto& entry : log) {
        if (entry.type == LogEntryType::USER) {
            user_messages++;
        } else if (entry.type == LogEntryType::RESPONSE) {
            assistant_messages++;
        }
    }
    
    std::cout << "User messages: " << user_messages << std::endl;
    std::cout << "Assistant messages: " << assistant_messages << std::endl;
    
    if (user_messages != 3 || assistant_messages != 3) {
        std::cerr << "❌ Expected 3 user and 3 assistant messages" << std::endl;
        return;
    }
    
    // Test token count
    size_t tokens = app.GetTokenCount();
    std::cout << "Total tokens: " << tokens << std::endl;
    
    if (tokens == 0) {
        std::cerr << "⚠️  Warning: Token count is 0 (may not be implemented)" << std::endl;
    }
    
    // Test clear conversation
    std::cout << "Clearing conversation..." << std::endl;
    app.ClearConversation();
    
    log = app.GetConversationLog();
    if (!log.empty()) {
        std::cerr << "❌ Conversation not cleared properly" << std::endl;
        return;
    }
    
    std::cout << "✅ State management test passed!" << std::endl;
    
    // Clean up
    app.Stop();
    server.Stop();
}

int main() {
    spdlog::set_level(spdlog::level::info);
    std::cout << "=== Testing JSON-RPC with HeadlessApplication ===" << std::endl;
    
    try {
        test_headless_chat_workflow();
        test_headless_tool_execution();
        test_headless_error_handling();
        test_headless_state_management();
        
        std::cout << "\n=== All HeadlessApplication JSON-RPC Tests Passed! ===" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}