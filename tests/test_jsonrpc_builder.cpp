#include <iostream>
#include <cassert>
#include <string>
#include "../src/protocol/jsonrpc_messages.hpp"
#include <json_struct.h>

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }
#define ASSERT_EQ(a, b) if ((a) != (b)) { FAIL("Expected " << (a) << " == " << (b)); }

int main() {
    using namespace jsonrpc;
    
    // ========================================================================
    // Test Request Building
    // ========================================================================
    
    TEST("Build simple request with ID")
    auto req1 = JsonRpcRequestBuilder()
        .method("test.method")
        .withId()
        .build();
    
    ASSERT_EQ(req1.jsonrpc, "2.0");
    ASSERT_EQ(req1.method, "test.method");
    ASSERT(req1.id.has_value());
    ASSERT(req1.id.value() > 0);
    PASS()
    
    TEST("Build notification (no ID)")
    auto req2 = JsonRpcRequestBuilder()
        .method("notify.something")
        .asNotification()
        .build();
    
    ASSERT_EQ(req2.jsonrpc, "2.0");
    ASSERT_EQ(req2.method, "notify.something");
    ASSERT(!req2.id.has_value());
    PASS()
    
    TEST("Build request with params struct")
    ChatParams chat_params;
    chat_params.content = "Hello world";
    chat_params.model = "kimi";
    chat_params.timeout = 30000;
    
    auto req3 = JsonRpcRequestBuilder()
        .method("chat.send")
        .withId()
        .params(chat_params)
        .build();
    
    ASSERT(req3.params.has_value());
    // Parse params back to verify
    ChatParams parsed_params;
    ASSERT(JsonRpcParser::parseParams(req3.params.value(), parsed_params));
    ASSERT_EQ(parsed_params.content, "Hello world");
    ASSERT_EQ(parsed_params.model.value(), "kimi");
    ASSERT_EQ(parsed_params.timeout.value(), 30000);
    PASS()
    
    TEST("Build request with raw JSON params")
    auto req4 = JsonRpcRequestBuilder()
        .method("raw.params")
        .withId()
        .paramsJson("{\"key\":\"value\"}")
        .build();
    
    ASSERT(req4.params.has_value());
    ASSERT_EQ(req4.params.value(), "{\"key\":\"value\"}");
    PASS()
    
    // ========================================================================
    // Test Serialization
    // ========================================================================
    
    TEST("Serialize request to JSON")
    auto req5 = JsonRpcRequestBuilder()
        .method("serialize.test")
        .id(42)
        .paramsJson("{\"test\":true}")
        .build();
    
    std::string json = jsonrpc::toCompactJson(req5);
    
    // Should contain all required fields (compact JSON)
    ASSERT(json.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
    ASSERT(json.find("\"id\":42") != std::string::npos);
    ASSERT(json.find("\"method\":\"serialize.test\"") != std::string::npos);
    ASSERT(json.find("\"params\":\"{\\\"test\\\":true}\"") != std::string::npos);
    PASS()
    
    TEST("Serialize notification to JSON")
    auto notif = JsonRpcNotificationBuilder()
        .method("tool.called")
        .paramsJson("{\"tool\":\"list_files\"}")
        .build();
    
    std::string notif_json = jsonrpc::toCompactJson(notif);
    
    // Should NOT have id field
    ASSERT(notif_json.find("\"id\"") == std::string::npos);
    ASSERT(notif_json.find("\"method\":\"tool.called\"") != std::string::npos);
    PASS()
    
    // ========================================================================
    // Test Deserialization
    // ========================================================================
    
    TEST("Parse request from JSON")
    std::string request_json = R"({
        "jsonrpc": "2.0",
        "id": 123,
        "method": "test.parse",
        "params": "{\"foo\":\"bar\"}"
    })";
    
    JsonRpcRequest parsed_req;
    ASSERT(JsonRpcParser::parseRequest(request_json, parsed_req));
    ASSERT_EQ(parsed_req.jsonrpc, "2.0");
    ASSERT_EQ(parsed_req.id.value(), 123);
    ASSERT_EQ(parsed_req.method, "test.parse");
    ASSERT_EQ(parsed_req.params.value(), "{\"foo\":\"bar\"}");
    PASS()
    
    TEST("Parse response from JSON")
    std::string response_json = R"({
        "jsonrpc": "2.0",
        "id": 456,
        "result": "{\"success\":true}"
    })";
    
    JsonRpcResponse parsed_resp;
    ASSERT(JsonRpcParser::parseResponse(response_json, parsed_resp));
    ASSERT_EQ(parsed_resp.jsonrpc, "2.0");
    ASSERT_EQ(parsed_resp.id, 456);
    ASSERT(parsed_resp.result.has_value());
    ASSERT(!parsed_resp.error.has_value());
    PASS()
    
    TEST("Parse error response from JSON")
    std::string error_json = R"({
        "jsonrpc": "2.0",
        "id": 789,
        "error": "{\"code\":-32601,\"message\":\"Method not found\"}"
    })";
    
    JsonRpcResponse error_resp;
    ASSERT(JsonRpcParser::parseResponse(error_json, error_resp));
    ASSERT(!error_resp.result.has_value());
    ASSERT(error_resp.error.has_value());
    
    // Parse the error details
    ErrorInfo error_info;
    ASSERT(JsonRpcParser::getError(error_resp, error_info));
    ASSERT_EQ(error_info.code, -32601);
    ASSERT_EQ(error_info.message, "Method not found");
    PASS()
    
    // ========================================================================
    // Test Response Building
    // ========================================================================
    
    TEST("Build success response")
    ChatResult result;
    result.reply = "Hello from server";
    result.timestamp = 1234567890;
    
    auto resp1 = JsonRpcResponseBuilder()
        .id(100)
        .result(result)
        .build();
    
    ASSERT_EQ(resp1.id, 100);
    ASSERT(resp1.result.has_value());
    ASSERT(!resp1.error.has_value());
    
    // Verify result can be parsed back
    ChatResult parsed_result;
    ASSERT(JsonRpcParser::getResult(resp1, parsed_result));
    ASSERT_EQ(parsed_result.reply, "Hello from server");
    ASSERT_EQ(parsed_result.timestamp, 1234567890);
    PASS()
    
    TEST("Build error response")
    auto resp2 = JsonRpcResponseBuilder()
        .id(200)
        .error(-32000, "Server error", "Additional details")
        .build();
    
    ASSERT_EQ(resp2.id, 200);
    ASSERT(!resp2.result.has_value());
    ASSERT(resp2.error.has_value());
    
    ErrorInfo parsed_error;
    ASSERT(JsonRpcParser::getError(resp2, parsed_error));
    ASSERT_EQ(parsed_error.code, -32000);
    ASSERT_EQ(parsed_error.message, "Server error");
    ASSERT_EQ(parsed_error.data.value(), "Additional details");
    PASS()
    
    // ========================================================================
    // Test Helper Methods
    // ========================================================================
    
    TEST("Helper: makeChat")
    auto chat_req = JsonRpcRequestBuilder::makeChat("Test message", "o3", 60000);
    
    ASSERT_EQ(chat_req.method, "chat.send");
    ASSERT(chat_req.id.has_value());
    ASSERT(chat_req.params.has_value());
    
    ChatParams chat_p;
    ASSERT(JsonRpcParser::parseParams(chat_req.params.value(), chat_p));
    ASSERT_EQ(chat_p.content, "Test message");
    ASSERT_EQ(chat_p.model.value(), "o3");
    ASSERT_EQ(chat_p.timeout.value(), 60000);
    PASS()
    
    TEST("Helper: makeHello")
    std::vector<std::string> caps = {"tools", "streaming"};
    auto hello_req = JsonRpcRequestBuilder::makeHello("1.0.0", caps);
    
    ASSERT_EQ(hello_req.method, "rpc.hello");
    ASSERT_EQ(hello_req.id.value(), 0);  // Special ID for hello
    ASSERT(hello_req.params.has_value());
    
    HelloParams hello_p;
    ASSERT(JsonRpcParser::parseParams(hello_req.params.value(), hello_p));
    ASSERT_EQ(hello_p.clientVersion, "1.0.0");
    ASSERT_EQ(hello_p.capabilities.size(), 2);
    ASSERT_EQ(hello_p.capabilities[0], "tools");
    ASSERT_EQ(hello_p.capabilities[1], "streaming");
    PASS()
    
    TEST("Helper: makeToolList")
    auto tool_req = JsonRpcRequestBuilder::makeToolList();
    
    ASSERT_EQ(tool_req.method, "tools.list");
    ASSERT(tool_req.id.has_value());
    ASSERT(tool_req.params.has_value());
    ASSERT_EQ(tool_req.params.value(), "{}");
    PASS()
    
    // ========================================================================
    // Test Round-trip (serialize and deserialize)
    // ========================================================================
    
    TEST("Round-trip: Request")
    // Build a complex request
    ChatParams original_params;
    original_params.content = "Round-trip test";
    original_params.model = "kimi";
    original_params.timeout = 90000;
    original_params.reasoning_effort = "high";
    
    auto original_req = JsonRpcRequestBuilder()
        .method("roundtrip.test")
        .id(999)
        .params(original_params)
        .build();
    
    // Serialize to JSON
    std::string json_str = jsonrpc::toCompactJson(original_req);
    
    // Parse back
    JsonRpcRequest parsed_req2;
    ASSERT(JsonRpcParser::parseRequest(json_str, parsed_req2));
    
    // Verify all fields match
    ASSERT_EQ(parsed_req2.jsonrpc, original_req.jsonrpc);
    ASSERT_EQ(parsed_req2.id.value(), original_req.id.value());
    ASSERT_EQ(parsed_req2.method, original_req.method);
    
    // Parse and verify params
    ChatParams parsed_params2;
    ASSERT(JsonRpcParser::parseParams(parsed_req2.params.value(), parsed_params2));
    ASSERT_EQ(parsed_params2.content, "Round-trip test");
    ASSERT_EQ(parsed_params2.model.value(), "kimi");
    ASSERT_EQ(parsed_params2.timeout.value(), 90000);
    ASSERT_EQ(parsed_params2.reasoning_effort.value(), "high");
    PASS()
    
    TEST("Round-trip: Response")
    // Build a response
    ChatResult original_result;
    original_result.reply = "Round-trip response";
    original_result.timestamp = 9876543210;
    original_result.streaming = true;
    original_result.streamId = "stream-123";
    
    auto original_resp = JsonRpcResponseBuilder()
        .id(888)
        .result(original_result)
        .build();
    
    // Serialize to JSON
    std::string resp_json_str = jsonrpc::toCompactJson(original_resp);
    
    // Parse back
    JsonRpcResponse parsed_resp2;
    ASSERT(JsonRpcParser::parseResponse(resp_json_str, parsed_resp2));
    
    // Verify all fields match
    ASSERT_EQ(parsed_resp2.jsonrpc, original_resp.jsonrpc);
    ASSERT_EQ(parsed_resp2.id, original_resp.id);
    ASSERT(parsed_resp2.result.has_value());
    ASSERT(!parsed_resp2.error.has_value());
    
    // Parse and verify result
    ChatResult parsed_result2;
    ASSERT(JsonRpcParser::getResult(parsed_resp2, parsed_result2));
    ASSERT_EQ(parsed_result2.reply, "Round-trip response");
    ASSERT_EQ(parsed_result2.timestamp, 9876543210);
    ASSERT_EQ(parsed_result2.streaming.value(), true);
    ASSERT_EQ(parsed_result2.streamId.value(), "stream-123");
    PASS()
    
    // ========================================================================
    // Test Edge Cases
    // ========================================================================
    
    TEST("Empty params handling")
    auto req_empty = JsonRpcRequestBuilder()
        .method("no.params")
        .withId()
        .build();
    
    ASSERT(!req_empty.params.has_value());
    
    std::string empty_json = jsonrpc::toCompactJson(req_empty);
    ASSERT(empty_json.find("\"params\"") == std::string::npos ||
           empty_json.find("\"params\":null") != std::string::npos);
    PASS()
    
    TEST("Null ID in notification")
    auto notif2 = JsonRpcNotificationBuilder()
        .method("test.notification")
        .build();
    
    std::string notif2_json = jsonrpc::toCompactJson(notif2);
    ASSERT(notif2_json.find("\"id\"") == std::string::npos);
    PASS()
    
    TEST("buildJson() method")
    auto req_json = JsonRpcRequestBuilder()
        .method("direct.json")
        .id(777)
        .buildJson();
    
    ASSERT(req_json.find("\"jsonrpc\":\"2.0\"") != std::string::npos);
    ASSERT(req_json.find("\"method\":\"direct.json\"") != std::string::npos);
    ASSERT(req_json.find("\"id\":777") != std::string::npos);
    PASS()
    
    std::cout << "\n=== All JSON-RPC builder tests passed! ===" << std::endl;
    return 0;
}