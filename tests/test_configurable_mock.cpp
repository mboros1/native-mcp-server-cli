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
    
    // Create MCP client for testing (constructor takes host and port)
    auto mcp_client = std::make_shared<MCPClient>("127.0.0.1", port);
    
    // Connect to mock server (Connect takes no arguments)
    mcp_client->Connect();
    
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
    server.RespondToChat("This is a configured response", true);
    
    // Send chat request
    auto chat_req = jsonrpc::JsonRpcRequestBuilder::makeChat("Hello server");
    mcp_client->SendRequest(jsonrpc::toCompactJson(chat_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify server received the request
    auto last_chat = server.GetLastRequest("chat.send");
    ASSERT(last_chat.has_value());
    
    // Parse params to verify content
    jsonrpc::ChatParams params;
    if (last_chat->params.has_value()) {
        jsonrpc::JsonRpcParser::parseParams(last_chat->params.value(), params);
        ASSERT(params.content == "Hello server");
    }
    PASS()
    
    // ========================================================================
    // Test 2: Configure error response
    // ========================================================================
    TEST("Configure error response")
    server.RespondWithError("tools.execute", -32003, "Tool execution failed");
    
    // Send tool execute request
    auto tool_req = jsonrpc::JsonRpcRequestBuilder()
        .method("tools.execute")
        .withId()
        .paramsJson("{\"name\":\"test_tool\"}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(tool_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify error was configured
    auto stats = server.GetStats();
    ASSERT(stats.method_counts.at("tools.execute") == 1);
    PASS()
    
    // ========================================================================
    // Test 3: Queue multiple responses
    // ========================================================================
    TEST("Queue multiple responses")
    // Queue 3 different responses for the same method
    server.RespondToChat("First response", true);
    server.RespondToChat("Second response", true);
    server.RespondToChat("Third response", true);
    
    // Send 3 chat requests
    for (int i = 0; i < 3; i++) {
        auto req = jsonrpc::JsonRpcRequestBuilder::makeChat("Message " + std::to_string(i));
        mcp_client->SendRequest(jsonrpc::toCompactJson(req));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Verify all 3 were processed
    auto final_stats = server.GetStats();
    ASSERT(final_stats.method_counts.at("chat.send") == 4); // 1 from test 1 + 3 from this test
    PASS()
    
    // ========================================================================
    // Test 4: Custom handler
    // ========================================================================
    TEST("Custom handler")
    // Set up a custom handler that echoes the input
    server.SetMethodHandler("echo.test", [](const jsonrpc::JsonRpcRequest& req) {
        jsonrpc::ChatResult result;
        result.reply = "Echo: " + req.params.value_or("empty");
        result.timestamp = 12345;
        
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    });
    
    // Send echo request
    auto echo_req = jsonrpc::JsonRpcRequestBuilder()
        .method("echo.test")
        .withId()
        .paramsJson("{\"message\":\"test echo\"}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(echo_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify it was processed
    ASSERT(server.GetStats().method_counts.at("echo.test") == 1);
    PASS()
    
    // ========================================================================
    // Test 5: Queue next response (any method)
    // ========================================================================
    TEST("Queue next response for any method")
    // Configure to return a specific response for the next request, regardless of method
    ConfigurableMockServer::MockResponse next_resp;
    next_resp.response = [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .resultJson("{\"special\":\"next response\"}")
            .buildJson();
    };
    next_resp.once = true;
    
    server.QueueNextResponse(next_resp);
    
    // Send any request
    auto any_req = jsonrpc::JsonRpcRequestBuilder()
        .method("any.method")
        .withId()
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(any_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify it was processed
    ASSERT(server.GetStats().method_counts.at("any.method") == 1);
    PASS()
    
    // ========================================================================
    // Test 6: Configure tool list response
    // ========================================================================
    TEST("Configure tool list response")
    std::vector<std::string> tools = {"list_files", "read_file", "write_file"};
    server.RespondWithTools(tools);
    
    // Send tool list request
    auto tools_req = jsonrpc::JsonRpcRequestBuilder::makeToolList();
    mcp_client->SendRequest(jsonrpc::toCompactJson(tools_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify request was processed
    ASSERT(server.GetStats().method_counts.at("tools.list") == 1);
    PASS()
    
    // ========================================================================
    // Test 7: Clear all responses
    // ========================================================================
    TEST("Clear all responses")
    // Configure some responses
    server.RespondToChat("Should be cleared", true);
    server.RespondWithError("test.method", -32000, "Should be cleared");
    
    // Clear all
    server.ClearAllResponses();
    
    // Send a request - should get method not found
    auto cleared_req = jsonrpc::JsonRpcRequestBuilder()
        .method("cleared.method")
        .withId()
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(cleared_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify it was processed but got default error
    ASSERT(server.GetStats().method_counts.at("cleared.method") == 1);
    PASS()
    
    // ========================================================================
    // Test 8: Request history
    // ========================================================================
    TEST("Request history tracking")
    // Send a few different requests
    auto req1 = jsonrpc::JsonRpcRequestBuilder()
        .method("history.test1")
        .withId()
        .build();
    
    auto req2 = jsonrpc::JsonRpcRequestBuilder()
        .method("history.test2")
        .withId()
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(req1));
    mcp_client->SendRequest(jsonrpc::toCompactJson(req2));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Check history
    auto history_stats = server.GetStats();
    ASSERT(history_stats.request_history.size() >= 2);
    
    // Find our specific requests in history
    bool found_test1 = false;
    bool found_test2 = false;
    for (const auto& req : history_stats.request_history) {
        if (req.method == "history.test1") found_test1 = true;
        if (req.method == "history.test2") found_test2 = true;
    }
    ASSERT(found_test1 && found_test2);
    PASS()
    
    // ========================================================================
    // Test 9: Default handlers (hello, sync)
    // ========================================================================
    TEST("Default handlers")
    // The server should have default handlers for basic methods
    
    // Test hello
    auto hello_req = jsonrpc::JsonRpcRequestBuilder::makeHello("1.0.0", {"test"});
    mcp_client->SendRequest(jsonrpc::toCompactJson(hello_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("rpc.hello") == 1);
    
    // Test sync
    auto sync_req = jsonrpc::JsonRpcRequestBuilder::makeSync();
    mcp_client->SendRequest(jsonrpc::toCompactJson(sync_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("history.sync") == 1);
    PASS()
    
    // ========================================================================
    // Print statistics
    // ========================================================================
    std::cout << "\n=== Server Statistics ===" << std::endl;
    auto final_stats_all = server.GetStats();
    std::cout << "Total requests: " << final_stats_all.total_requests << std::endl;
    std::cout << "Method counts:" << std::endl;
    for (const auto& [method, count] : final_stats_all.method_counts) {
        std::cout << "  " << method << ": " << count << std::endl;
    }
    
    // Cleanup
    mcp_client->Stop();
    server.Stop();
    
    std::cout << "\n=== All configurable mock server tests passed! ===" << std::endl;
    return 0;
}