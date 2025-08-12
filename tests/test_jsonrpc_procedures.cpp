/**
 * Test for JSON-RPC procedures with type-safe interface
 */

#include "../src/network/mcp_client.hpp"
#include "../src/protocol/jsonrpc_procedures.hpp"
#include "../tests/configurable_mock_server.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>
#include <set>
#include <vector>

void test_type_safe_chat() {
    std::cout << "\nTesting type-safe CHAT_SEND procedure..." << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Configure chat response
    jsonrpc::ChatResult expected_result{
        .reply = "Hello from type-safe interface!",
        .timestamp = 1234567890,
        .streaming = false
    };
    
    ConfigurableMockServer::MockResponse chat_response;
    chat_response.response = [expected_result](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(expected_result)
            .buildJson();
    };
    server.SetMethodDefault("chat.send", chat_response);
    
    // Create client
    MCPClient client("127.0.0.1", port);
    
    // Track responses
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    std::string received_reply;
    
    // Set a basic response callback (needed for processing to work)
    client.SetResponseCallback([](const std::string&, const std::string&) {
        // The new interface will handle responses through the registry
    });
    
    // Start the client
    if (!client.Connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return;
    }
    
    // Use the type-safe interface
    client.Call(jsonrpc::CHAT_SEND,
        jsonrpc::ChatParams{
            .content = "Hello world!",
            .model = "test-model",
            .timeout = 30
        },
        [&](const jsonrpc::ChatResult& result) {
            received_reply = result.reply;
            success_called = true;
            std::cout << "Success callback called with: " << result.reply << std::endl;
        },
        [&](int code, const std::string& message) {
            error_called = true;
            std::cerr << "Error callback called: " << code << " - " << message << std::endl;
        }
    );
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify
    assert(success_called && "Success callback should have been called");
    assert(!error_called && "Error callback should not have been called");
    assert(received_reply == expected_result.reply && "Reply should match expected");
    
    std::cout << "✅ Type-safe CHAT_SEND works!" << std::endl;
    
    client.Stop();
    server.Stop();
}

void test_type_safe_tools_list() {
    std::cout << "\nTesting type-safe TOOLS_LIST procedure..." << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Configure tools response
    jsonrpc::ToolListResult expected_result{
        .tools = {"search", "calculator", "weather"},
        .timestamp = 1234567890
    };
    
    ConfigurableMockServer::MockResponse tools_response;
    tools_response.response = [expected_result](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(expected_result)
            .buildJson();
    };
    server.SetMethodDefault("tools.list", tools_response);
    
    // Create client
    MCPClient client("127.0.0.1", port);
    
    // Track responses
    std::atomic<bool> success_called{false};
    std::vector<std::string> received_tools;
    
    // Start the client
    if (!client.Connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return;
    }
    
    // Use the type-safe interface (no params needed)
    client.Call(jsonrpc::TOOLS_LIST,
        [&](const jsonrpc::ToolListResult& result) {
            received_tools = result.tools;
            success_called = true;
            std::cout << "Received " << result.tools.size() << " tools" << std::endl;
        },
        nullptr  // Optional error callback
    );
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify
    assert(success_called && "Success callback should have been called");
    assert(received_tools.size() == 3 && "Should have received 3 tools");
    assert(received_tools[0] == "search" && "First tool should be search");
    
    std::cout << "✅ Type-safe TOOLS_LIST works!" << std::endl;
    
    client.Stop();
    server.Stop();
}

void test_type_safe_error_handling() {
    std::cout << "\nTesting type-safe error handling..." << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Configure error response
    server.RespondWithError("chat.send", -32001, "Request timed out");
    
    // Create client
    MCPClient client("127.0.0.1", port);
    
    // Track responses
    std::atomic<bool> success_called{false};
    std::atomic<bool> error_called{false};
    int received_code = 0;
    std::string received_message;
    
    // Start the client
    if (!client.Connect()) {
        std::cerr << "Failed to connect to server" << std::endl;
        return;
    }
    
    // Use the type-safe interface
    client.Call(jsonrpc::CHAT_SEND,
        jsonrpc::ChatParams{
            .content = "This will timeout",
            .model = "test-model"
        },
        [&](const jsonrpc::ChatResult& result) {
            success_called = true;
            std::cout << "Unexpected success: " << result.reply << std::endl;
        },
        [&](int code, const std::string& message) {
            received_code = code;
            received_message = message;
            error_called = true;
            std::cout << "Error callback called: " << code << " - " << message << std::endl;
        }
    );
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Verify
    assert(!success_called && "Success callback should not have been called");
    assert(error_called && "Error callback should have been called");
    assert(received_code == -32001 && "Error code should be -32001");
    assert(received_message == "Request timed out" && "Error message should match");
    
    std::cout << "✅ Type-safe error handling works!" << std::endl;
    
    client.Stop();
    server.Stop();
}

void test_random_id_generation() {
    std::cout << "\nTesting random ID generation..." << std::endl;
    
    // Generate multiple IDs and check they're different
    std::set<int64_t> ids;
    for (int i = 0; i < 100; ++i) {
        int64_t id = jsonrpc::GenerateRequestId();
        assert(id > 0 && "ID should be positive");
        assert(ids.find(id) == ids.end() && "ID should be unique");
        ids.insert(id);
    }
    
    std::cout << "✅ Generated 100 unique random IDs" << std::endl;
}

int main() {
    std::cout << "=== Testing JSON-RPC Procedures with Type-Safe Interface ===" << std::endl;
    
    try {
        test_random_id_generation();
        test_type_safe_chat();
        test_type_safe_tools_list();
        test_type_safe_error_handling();
        
        std::cout << "\n=== All Type-Safe Procedure Tests Passed! ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}