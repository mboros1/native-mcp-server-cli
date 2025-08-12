#include <iostream>
#include <cassert>
#include <string>
#include "../src/protocol/jsonrpc_messages.hpp"

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

int main() {
    using namespace jsonrpc;
    
    // Test basic request building
    TEST("Build request with ID")
    auto req1 = JsonRpcRequestBuilder()
        .method("test.method")
        .id(123)
        .build();
    
    ASSERT(req1.jsonrpc == "2.0");
    ASSERT(req1.method == "test.method");
    ASSERT(req1.id.has_value());
    ASSERT(req1.id.value() == 123);
    PASS()
    
    // Test notification (no ID)
    TEST("Build notification")
    auto req2 = JsonRpcRequestBuilder()
        .method("notify.something")
        .build();
    
    ASSERT(!req2.id.has_value());
    PASS()
    
    // Test request with ChatParams
    TEST("Build chat request")
    ChatParams params{
        .content = "Hello",
        .model = "test",
        .timeout = 30000
    };
    
    auto json = JsonRpcRequestBuilder()
        .method("chat.send")
        .id(456)
        .params(params)
        .buildJson();
    
    ASSERT(json.find("\"method\":\"chat.send\"") != std::string::npos);
    ASSERT(json.find("\"id\":456") != std::string::npos);
    ASSERT(json.find("\"content\":\"Hello\"") != std::string::npos);
    PASS()
    
    // Test response building
    TEST("Build success response")
    ChatResult result{
        .reply = "Response",
        .timestamp = 123456,
        .streaming = false
    };
    
    auto resp_json = JsonRpcResponseBuilder()
        .id(789)
        .result(result)
        .buildJson();
    
    ASSERT(resp_json.find("\"id\":789") != std::string::npos);
    ASSERT(resp_json.find("\"reply\":\"Response\"") != std::string::npos);
    PASS()
    
    // Test error response
    TEST("Build error response")
    auto err_json = JsonRpcResponseBuilder()
        .id(999)
        .error(-32601, "Method not found")
        .buildJson();
    
    ASSERT(err_json.find("\"id\":999") != std::string::npos);
    ASSERT(err_json.find("\"code\":-32601") != std::string::npos);
    ASSERT(err_json.find("\"message\":\"Method not found\"") != std::string::npos);
    PASS()
    
    // Test ID generation
    TEST("Generate random ID")
    auto id1 = GenerateRequestId();
    auto id2 = GenerateRequestId();
    
    ASSERT(id1 > 0);
    ASSERT(id2 > 0);
    ASSERT(id1 != id2); // Should be different (very high probability)
    PASS()
    
    std::cout << "\n=== All JSON-RPC builder tests passed! ===" << std::endl;
    return 0;
}