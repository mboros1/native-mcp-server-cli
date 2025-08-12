#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "configurable_mock_server.hpp"
#include "../src/protocol/jsonrpc_messages.hpp"
#include "../src/network/mcp_client.hpp"

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

int main() {
    // Start configurable mock server
    ConfigurableMockServer server;
    unsigned short port = server.Start();
    
    // Create MCP client for testing
    MCPClient client("127.0.0.1", port);
    
    // Connect to mock server
    if (!client.Connect()) {
        std::cerr << "Failed to connect to mock server" << std::endl;
        return 1;
    }
    
    // Wait for connection
    if (!server.WaitForConnection(5000)) {
        std::cerr << "Failed to establish connection to mock server" << std::endl;
        return 1;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // ========================================================================
    // Test 1: Configure specific chat response
    // ========================================================================
    TEST("Configure specific chat response")
    
    // Configure server to respond with a specific chat result
    jsonrpc::ChatResult expected_result{
        .reply = "This is a configured response",
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
    
    // Send chat request using the new builder
    jsonrpc::ChatParams chat_params{
        .content = "Hello server",
        .model = "test-model",
        .timeout = 30000
    };
    
    auto chat_req = jsonrpc::JsonRpcRequestBuilder()
        .method("chat.send")
        .id(123)
        .params(chat_params)
        .buildJson();
    
    client.SendRequest(chat_req);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify server received the request
    auto last_chat = server.GetLastRequest("chat.send");
    ASSERT(last_chat.has_value());
    ASSERT(last_chat->method == "chat.send");
    ASSERT(last_chat->id.has_value());
    PASS()
    
    // ========================================================================
    // Test 2: Configure error response
    // ========================================================================
    TEST("Configure error response")
    
    // Configure server to return an error for tools.execute
    ConfigurableMockServer::MockResponse error_response;
    error_response.response = [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .error(-32003, "Tool execution failed")
            .buildJson();
    };
    server.SetMethodDefault("tools.execute", error_response);
    
    // Send tool execute request
    auto tool_req = jsonrpc::JsonRpcRequestBuilder()
        .method("tools.execute")
        .id(456)
        .buildJson();
    
    client.SendRequest(tool_req);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify error was configured
    auto stats = server.GetStats();
    ASSERT(stats.method_counts.count("tools.execute") > 0);
    ASSERT(stats.method_counts.at("tools.execute") == 1);
    PASS()
    
    // ========================================================================
    // Test 3: Queue multiple responses
    // ========================================================================
    TEST("Queue multiple responses")
    
    // Queue 3 different responses for the same method
    for (int i = 1; i <= 3; i++) {
        ConfigurableMockServer::MockResponse response;
        response.once = true;  // Use once then remove
        response.response = [i](const jsonrpc::JsonRpcRequest& req) {
            jsonrpc::ChatResult result{
                .reply = "Response #" + std::to_string(i),
                .timestamp = 1234567890 + i,
                .streaming = false
            };
            return jsonrpc::JsonRpcResponseBuilder()
                .id(req.id.value_or(0))
                .result(result)
                .buildJson();
        };
        server.QueueMethodResponse("chat.send", response);
    }
    
    // Send 3 chat requests
    for (int i = 0; i < 3; i++) {
        jsonrpc::ChatParams params{
            .content = "Message " + std::to_string(i),
            .model = "test-model"
        };
        
        auto req = jsonrpc::JsonRpcRequestBuilder()
            .method("chat.send")
            .id(1000 + i)
            .params(params)
            .buildJson();
        
        client.SendRequest(req);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Verify all 3 were processed
    auto final_stats = server.GetStats();
    ASSERT(final_stats.method_counts.at("chat.send") == 4); // 1 from test 1 + 3 from this test
    PASS()
    
    // ========================================================================
    // Test 4: Test statistics tracking
    // ========================================================================
    TEST("Statistics tracking")
    
    auto tracking_stats = server.GetStats();
    ASSERT(tracking_stats.total_requests >= 5); // At least 5 requests sent
    ASSERT(tracking_stats.method_counts.size() >= 2); // At least 2 different methods
    ASSERT(tracking_stats.method_counts["chat.send"] == 4);
    ASSERT(tracking_stats.method_counts["tools.execute"] == 1);
    PASS()
    
    // ========================================================================
    // Test 5: Get last request for specific method
    // ========================================================================
    TEST("Get last request")
    
    // Send a unique request
    jsonrpc::ToolListResult tool_list_result{
        .tools = {"file_reader", "calculator"},
        .timestamp = 9999999
    };
    
    ConfigurableMockServer::MockResponse tools_response;
    tools_response.response = [tool_list_result](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(tool_list_result)
            .buildJson();
    };
    server.SetMethodDefault("tools.list", tools_response);
    
    auto tools_req = jsonrpc::JsonRpcRequestBuilder()
        .method("tools.list")
        .id(9999)
        .buildJson();
    
    client.SendRequest(tools_req);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Get last tools.list request
    auto last_tools = server.GetLastRequest("tools.list");
    ASSERT(last_tools.has_value());
    ASSERT(last_tools->method == "tools.list");
    ASSERT(last_tools->id.value() == 9999);
    PASS()
    
    // ========================================================================
    // Test 6: Default hello response
    // ========================================================================
    TEST("Default hello response")
    
    // Send hello request
    jsonrpc::HelloParams hello_params{
        .clientVersion = "1.0.0",
        .capabilities = {"chat", "tools"}
    };
    
    auto hello_req = jsonrpc::JsonRpcRequestBuilder()
        .method("rpc.hello")
        .id(0)
        .params(hello_params)
        .buildJson();
    
    client.SendRequest(hello_req);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify hello was processed
    auto hello_stats = server.GetStats();
    ASSERT(hello_stats.method_counts.count("rpc.hello") > 0);
    PASS()
    
    // Clean up
    server.Stop();
    
    std::cout << "\n=== All configurable mock server tests passed! ===" << std::endl;
    return 0;
}