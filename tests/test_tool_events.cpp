/**
 * Test tool event handling with JSON-RPC 2.0
 */

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "../src/network/mcp_client.hpp"
#include "../src/protocol/jsonrpc_messages.hpp"
#include "../src/protocol/jsonrpc_procedures.hpp"
#include "configurable_mock_server.hpp"

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

int main() {
    // Create mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Create MCP client
    MCPClient client("127.0.0.1", port);
    
    // Connect to server
    if (!client.Connect()) {
        std::cerr << "Failed to connect to mock server" << std::endl;
        return 1;
    }
    
    // Wait for connection
    server.WaitForConnection(5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // ========================================================================
    // Test 1: Tool list request/response
    // ========================================================================
    TEST("Tool list request")
    
    // Configure server to return tool list
    jsonrpc::ToolListResult tool_list{
        .tools = {"file_reader", "calculator", "web_search"},
        .timestamp = 1234567890
    };
    
    ConfigurableMockServer::MockResponse tools_response;
    tools_response.response = [tool_list](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(tool_list)
            .buildJson();
    };
    server.SetMethodDefault("tools.list", tools_response);
    
    // Track response
    bool got_tools = false;
    std::vector<std::string> received_tools;
    
    // Use the type-safe procedure call
    client.Call(jsonrpc::TOOLS_LIST, std::monostate{},
        [&](const jsonrpc::ToolListResult& result) {
            got_tools = true;
            received_tools = result.tools;
        },
        [](int, const std::string& error) {
            std::cerr << "Tools list error: " << error << std::endl;
        }
    );
    
    // Wait for response
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    ASSERT(got_tools);
    ASSERT(received_tools.size() == 3);
    ASSERT(received_tools[0] == "file_reader");
    PASS()
    
    // ========================================================================
    // Test 2: Tool execute request/response
    // ========================================================================
    TEST("Tool execute request")
    
    // Configure server to return tool execution result
    jsonrpc::ToolExecuteResult exec_result{
        .success = true,
        .output = "File contents: Hello World",
        .error = std::nullopt,
        .execution_time_ms = 50
    };
    
    ConfigurableMockServer::MockResponse exec_response;
    exec_response.response = [exec_result](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(exec_result)
            .buildJson();
    };
    server.SetMethodDefault("tools.execute", exec_response);
    
    // Send tool execute request
    jsonrpc::ToolExecuteParams exec_params{
        .name = "file_reader",
        .arguments = R"({"path": "/test.txt"})"
    };
    
    auto exec_req = jsonrpc::JsonRpcRequestBuilder()
        .method("tools.execute")
        .id(200)
        .params(exec_params)
        .buildJson();
    
    client.SendRequest(exec_req);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify request was received
    auto last_exec = server.GetLastRequest("tools.execute");
    ASSERT(last_exec.has_value());
    ASSERT(last_exec->method == "tools.execute");
    PASS()
    
    // ========================================================================
    // Test 3: Tool error handling
    // ========================================================================
    TEST("Tool error handling")
    
    // Configure server to return error
    ConfigurableMockServer::MockResponse error_response;
    error_response.response = [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .error(-32002, "Tool not found")
            .buildJson();
    };
    server.SetMethodDefault("tools.execute", error_response);
    
    // Track error
    bool got_error = false;
    int error_code = 0;
    
    // Send request with callback
    client.SetResponseCallback([&](const std::string& type, const std::string&) {
        if (type == "ERROR") {
            got_error = true;
        }
    });
    
    // Send bad tool request
    auto bad_req = jsonrpc::JsonRpcRequestBuilder()
        .method("tools.execute")
        .id(300)
        .buildJson();
    
    client.SendRequest(bad_req);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify error was sent
    auto stats = server.GetStats();
    ASSERT(stats.method_counts["tools.execute"] == 2); // One from test 2, one from this test
    PASS()
    
    // ========================================================================
    // Test 4: Tool cancel request
    // ========================================================================
    TEST("Tool cancel request")
    
    // Send cancel request
    jsonrpc::CancelParams cancel_params{
        .request_id = 999
    };
    
    auto cancel_req = jsonrpc::JsonRpcRequestBuilder()
        .method("request.cancel")
        .id(400)
        .params(cancel_params)
        .buildJson();
    
    client.SendRequest(cancel_req);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify cancel was received
    auto last_cancel = server.GetLastRequest("request.cancel");
    ASSERT(last_cancel.has_value());
    ASSERT(last_cancel->method == "request.cancel");
    PASS()
    
    // ========================================================================
    // Test 5: Multiple tool responses
    // ========================================================================
    TEST("Multiple tool responses")
    
    // Queue multiple tool results
    for (int i = 1; i <= 3; i++) {
        ConfigurableMockServer::MockResponse response;
        response.once = true;
        response.response = [i](const jsonrpc::JsonRpcRequest& req) {
            jsonrpc::ToolExecuteResult result{
                .success = true,
                .output = "Tool output #" + std::to_string(i),
                .error = std::nullopt,
                .execution_time_ms = i * 10
            };
            return jsonrpc::JsonRpcResponseBuilder()
                .id(req.id.value_or(0))
                .result(result)
                .buildJson();
        };
        server.QueueMethodResponse("tools.execute", response);
    }
    
    // Send 3 tool requests
    for (int i = 0; i < 3; i++) {
        jsonrpc::ToolExecuteParams params{
            .name = "tool_" + std::to_string(i),
            .arguments = "{}"
        };
        
        auto req = jsonrpc::JsonRpcRequestBuilder()
            .method("tools.execute")
            .id(500 + i)
            .params(params)
            .buildJson();
        
        client.SendRequest(req);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // Verify all were processed
    auto final_stats = server.GetStats();
    ASSERT(final_stats.method_counts["tools.execute"] >= 5); // 2 from earlier + 3 from this test
    PASS()
    
    // Clean up
    server.Stop();
    
    std::cout << "\n=== All tool event tests passed! ===" << std::endl;
    return 0;
}