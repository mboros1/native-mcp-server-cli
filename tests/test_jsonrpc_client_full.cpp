#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>
#include "configurable_mock_server.hpp"
#include "../src/protocol/jsonrpc_messages.hpp"
#include "../src/network/mcp_client.hpp"

// Additional types for testing correlation
struct ToolEventParams {
    std::string tool_name;
    std::optional<std::string> arguments;
    int64_t timestamp;
    std::optional<int> request_id;  // For correlation
    
    JS_OBJ(tool_name, arguments, timestamp, request_id);
};

struct StreamChunkParams {
    std::string streamId;  // For correlation
    std::string chunk;
    int index;
    
    JS_OBJ(streamId, chunk, index);
};

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

// Test context for capturing responses
struct TestContext {
    std::vector<std::pair<std::string, std::string>> received_callbacks;
    
    void onResponse(const std::string& type, const std::string& content) {
        received_callbacks.push_back({type, content});
        std::cout << "  Received: type=" << type << ", content=" << content.substr(0, 50) << "..." << std::endl;
    }
    
    void clearCallbacks() {
        received_callbacks.clear();
    }
    
    bool hasResponse() const {
        return !received_callbacks.empty();
    }
};

int main() {
    // Start configurable mock server
    ConfigurableMockServer server;
    unsigned short port = server.Start();
    
    // Create test context
    TestContext context;
    
    // Create MCP client
    auto mcp_client = std::make_shared<MCPClient>("127.0.0.1", port);
    mcp_client->SetResponseCallback([&context](const std::string& type, const std::string& content) {
        context.onResponse(type, content);
    });
    
    // Connect to mock server
    mcp_client->Connect();
    
    // Wait for connection
    if (!server.WaitForConnection(5000)) {
        std::cerr << "Failed to establish connection to mock server" << std::endl;
        return 1;
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // ========================================================================
    // Test 1: Hello/Handshake
    // ========================================================================
    TEST("JSON-RPC Hello/Handshake")
    
    // Configure mock response for hello
    server.SetMethodHandler("rpc.hello", [](const jsonrpc::JsonRpcRequest& req) {
        jsonrpc::HelloResult result;
        result.version = "2.0";
        result.serverVersion = "1.0.0-mock";
        result.capabilities = {"chat", "tools", "sync", "retry"};
        result.sessionId = "test-session-123";
        
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    });
    
    // Send hello request
    auto hello_req = jsonrpc::JsonRpcRequestBuilder::makeHello("1.0.0", {"client-cap"});
    mcp_client->SendRequest(jsonrpc::toCompactJson(hello_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("rpc.hello") == 1);
    PASS()
    
    // ========================================================================
    // Test 2: Chat Message
    // ========================================================================
    TEST("Chat Message Request")
    context.clearCallbacks();
    
    // Configure mock response for chat
    server.SetMethodHandler("chat.send", [](const jsonrpc::JsonRpcRequest& req) {
        jsonrpc::ChatResult result;
        result.reply = "This is the assistant's response to your message";
        result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    });
    
    // Send chat request using builder
    auto chat_req = jsonrpc::JsonRpcRequestBuilder::makeChat("Hello, assistant!");
    mcp_client->SendRequest(jsonrpc::toCompactJson(chat_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("chat.send") == 1);
    // Note: Response handling will be tested once MCPClient is updated
    PASS()
    
    // ========================================================================
    // Test 3: Chat Retry
    // ========================================================================
    TEST("Chat Retry Request")
    
    server.SetMethodHandler("chat.retry", [](const jsonrpc::JsonRpcRequest& req) {
        jsonrpc::ChatResult result;
        result.reply = "Here's a new response to your retry request";
        result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    });
    
    // Send retry request
    auto retry_req = jsonrpc::JsonRpcRequestBuilder()
        .method("chat.retry")
        .withId()
        .paramsJson("{\"originalMessage\":\"Previous message\",\"model\":\"kimi\",\"reasoning_effort\":\"medium\"}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(retry_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("chat.retry") == 1);
    PASS()
    
    // ========================================================================
    // Test 4: Interrupt Request
    // ========================================================================
    TEST("Interrupt/Cancel Request")
    
    server.SetMethodHandler("request.cancel", [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .resultJson("{\"success\":true,\"message\":\"Request cancelled\"}")
            .buildJson();
    });
    
    // Send interrupt request
    auto interrupt_req = jsonrpc::JsonRpcRequestBuilder()
        .method("request.cancel")
        .withId()
        .paramsJson("{\"requestId\":1}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(interrupt_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("request.cancel") == 1);
    PASS()
    
    // ========================================================================
    // Test 5: History Sync
    // ========================================================================
    TEST("History Sync Request")
    
    server.SetMethodHandler("history.sync", [](const jsonrpc::JsonRpcRequest& req) {
        jsonrpc::SyncResult result;
        result.entries = 10;
        result.messages = {"user: Hello", "assistant: Hi there", "user: How are you?"};
        result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    });
    
    // Send sync request
    auto sync_req = jsonrpc::JsonRpcRequestBuilder::makeSync();
    mcp_client->SendRequest(jsonrpc::toCompactJson(sync_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("history.sync") == 1);
    PASS()
    
    // ========================================================================
    // Test 6: History Reload
    // ========================================================================
    TEST("History Reload Request")
    
    server.SetMethodHandler("history.reload", [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .resultJson("{\"success\":true,\"entriesLoaded\":15}")
            .buildJson();
    });
    
    // Send reload request
    auto reload_req = jsonrpc::JsonRpcRequestBuilder()
        .method("history.reload")
        .withId()
        .paramsJson("{\"source\":\"file\"}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(reload_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("history.reload") == 1);
    PASS()
    
    // ========================================================================
    // Test 7: Tool List
    // ========================================================================
    TEST("Tool List Request")
    
    server.SetMethodHandler("tools.list", [](const jsonrpc::JsonRpcRequest& req) {
        jsonrpc::ToolListResult result;
        result.tools = {"list_files", "read_file", "write_file", "execute_command"};
        result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .result(result)
            .buildJson();
    });
    
    // Send tool list request
    auto tools_req = jsonrpc::JsonRpcRequestBuilder::makeToolList();
    mcp_client->SendRequest(jsonrpc::toCompactJson(tools_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("tools.list") == 1);
    PASS()
    
    // ========================================================================
    // Test 8: Tool Execution
    // ========================================================================
    TEST("Tool Execution Request")
    
    server.SetMethodHandler("tools.execute", [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .resultJson("{\"tool_name\":\"list_files\",\"success\":true,\"result\":{\"files\":[\"file1.txt\",\"file2.cpp\"]}}")
            .buildJson();
    });
    
    // Send tool execution request
    auto tool_req = jsonrpc::JsonRpcRequestBuilder()
        .method("tools.execute")
        .withId()
        .paramsJson("{\"name\":\"list_files\",\"arguments\":{\"path\":\"/tmp\"}}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(tool_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("tools.execute") == 1);
    PASS()
    
    // ========================================================================
    // Test 9: New Conversation
    // ========================================================================
    TEST("New Conversation Request")
    
    server.SetMethodHandler("chat.new", [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .resultJson("{\"success\":true,\"message\":\"Started new conversation\",\"sessionId\":\"new-session-456\"}")
            .buildJson();
    });
    
    // Send new conversation request
    auto new_req = jsonrpc::JsonRpcRequestBuilder()
        .method("chat.new")
        .withId()
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(new_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("chat.new") == 1);
    PASS()
    
    // ========================================================================
    // Test 10: Session Reset
    // ========================================================================
    TEST("Session Reset Request")
    
    server.SetMethodHandler("session.reset", [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .resultJson("{\"success\":true,\"message\":\"Session state reset\"}")
            .buildJson();
    });
    
    // Send reset request
    auto reset_req = jsonrpc::JsonRpcRequestBuilder()
        .method("session.reset")
        .withId()
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(reset_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("session.reset") == 1);
    PASS()
    
    // ========================================================================
    // Test 11: Error Handling
    // ========================================================================
    TEST("Error Response Handling")
    
    server.SetMethodHandler("test.error", [](const jsonrpc::JsonRpcRequest& req) {
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .error(-32601, "Method not found", "{\"attempted_method\":\"unknown\"}")
            .buildJson();
    });
    
    // Send request that will trigger error
    auto error_req = jsonrpc::JsonRpcRequestBuilder()
        .method("test.error")
        .withId()
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(error_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("test.error") == 1);
    PASS()
    
    // ========================================================================
    // Test 12: Server-to-Client Events (with proper correlation)
    // ========================================================================
    TEST("Server Events with Request Correlation")
    
    // In a real implementation, server would send these as separate messages
    // For testing, we'll verify the structure includes request_id for correlation
    
    // Tool events should include request_id for correlation
    ToolEventParams tool_params;
    tool_params.tool_name = "list_files";
    tool_params.arguments = "{\"path\":\"/home\"}";
    tool_params.timestamp = 123456789;
    tool_params.request_id = 5;  // Links back to chat request #5
    
    // Verify serialization includes request_id
    std::string tool_json = jsonrpc::toCompactJson(tool_params);
    ASSERT(tool_json.find("\"request_id\":5") != std::string::npos);
    
    // Stream chunks already have streamId for correlation
    StreamChunkParams stream_params;
    stream_params.streamId = "stream-5";  // Links to original request's streamId
    stream_params.chunk = "First part of response...";
    stream_params.index = 0;
    
    std::string stream_json = jsonrpc::toCompactJson(stream_params);
    ASSERT(stream_json.find("\"streamId\":\"stream-5\"") != std::string::npos);
    PASS()
    
    // ========================================================================
    // Test 13: Batch Requests
    // ========================================================================
    TEST("Batch JSON-RPC Requests")
    
    // Note: This would require batch support in MCPClient
    // For now, just test that server can handle individual requests in sequence
    
    auto batch1 = jsonrpc::JsonRpcRequestBuilder::makeSync();
    auto batch2 = jsonrpc::JsonRpcRequestBuilder::makeToolList();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(batch1));
    mcp_client->SendRequest(jsonrpc::toCompactJson(batch2));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Check that both requests were processed (should be 2 for sync, 2 for tools.list)
    auto stats = server.GetStats();
    ASSERT(stats.method_counts.at("history.sync") >= 2); // At least 2 calls
    ASSERT(stats.method_counts.at("tools.list") >= 2); // At least 2 calls
    PASS()
    
    // ========================================================================
    // Test 14: Tool Events (as notifications from server)
    // ========================================================================
    TEST("Tool Event Notifications")
    
    // Configure server to send tool event notifications
    server.SetMethodHandler("chat.with_tools", [&mcp_client](const jsonrpc::JsonRpcRequest& req) {
        // First send tool call notification
        auto tool_call_notif = jsonrpc::JsonRpcNotificationBuilder()
            .method("tool.called")
            .paramsJson("{\"tool_name\":\"list_files\",\"arguments\":{\"path\":\"/home\"}}")
            .build();
        
        // Note: In real implementation, server would send this separately
        // For test purposes, we're just building the notification structure
        
        // Return chat response with tool info
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .resultJson("{\"reply\":\"I've listed the files for you\",\"tools_used\":[\"list_files\"]}")
            .buildJson();
    });
    
    auto tool_chat_req = jsonrpc::JsonRpcRequestBuilder()
        .method("chat.with_tools")
        .withId()
        .paramsJson("{\"content\":\"List files in home directory\"}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(tool_chat_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("chat.with_tools") == 1);
    PASS()
    
    // ========================================================================
    // Test 15: Timeout Handling
    // ========================================================================
    TEST("Timeout Response Handling")
    
    server.SetMethodHandler("chat.timeout", [](const jsonrpc::JsonRpcRequest& req) {
        // Return timeout error with retry information
        return jsonrpc::JsonRpcResponseBuilder()
            .id(req.id.value_or(0))
            .error(-32000, "Request timeout", "{\"canRetry\":true,\"originalMessage\":\"Test message\",\"timeout\":120000}")
            .buildJson();
    });
    
    auto timeout_req = jsonrpc::JsonRpcRequestBuilder()
        .method("chat.timeout")
        .withId()
        .paramsJson("{\"content\":\"This will timeout\"}")
        .build();
    
    mcp_client->SendRequest(jsonrpc::toCompactJson(timeout_req));
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT(server.GetStats().method_counts.at("chat.timeout") == 1);
    PASS()
    
    // ========================================================================
    // Print statistics
    // ========================================================================
    std::cout << "\n=== Server Statistics ===" << std::endl;
    auto final_stats = server.GetStats();
    std::cout << "Total requests: " << final_stats.total_requests << std::endl;
    std::cout << "Method counts:" << std::endl;
    for (const auto& [method, count] : final_stats.method_counts) {
        std::cout << "  " << method << ": " << count << std::endl;
    }
    
    // Cleanup
    mcp_client->Stop();
    server.Stop();
    
    std::cout << "\n=== All JSON-RPC client tests passed! ===" << std::endl;
    std::cout << "\nJSON-RPC 2.0 Method Mapping:" << std::endl;
    std::cout << "  Old Format              -> JSON-RPC 2.0 Method" << std::endl;
    std::cout << "  -------------------------------------------" << std::endl;
    std::cout << "  type:chat               -> chat.send" << std::endl;
    std::cout << "  type:retry              -> chat.retry" << std::endl;
    std::cout << "  type:interrupt          -> request.cancel" << std::endl;
    std::cout << "  type:sync               -> history.sync" << std::endl;
    std::cout << "  type:reload             -> history.reload" << std::endl;
    std::cout << "  /new command            -> chat.new" << std::endl;
    std::cout << "  type:reset              -> session.reset" << std::endl;
    std::cout << "  tools/list (already ok) -> tools.list" << std::endl;
    std::cout << "  tools/call              -> tools.execute" << std::endl;
    std::cout << "\nResponse formats now use JSON-RPC 2.0 structure:" << std::endl;
    std::cout << "  {\"jsonrpc\":\"2.0\",\"id\":N,\"result\":{...}}" << std::endl;
    std::cout << "  {\"jsonrpc\":\"2.0\",\"id\":N,\"error\":{\"code\":N,\"message\":\"...\"}}" << std::endl;
    
    return 0;
}