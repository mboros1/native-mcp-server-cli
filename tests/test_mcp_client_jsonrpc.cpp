#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include "../src/network/mcp_client.hpp"
#include "../tests/configurable_mock_server.hpp"

bool test_json_rpc_chat_response() {
    std::cout << "Testing JSON-RPC 2.0 chat response parsing..." << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Queue a JSON-RPC response for chat
    jsonrpc::ChatResult chat_result{
        .reply = "Hello from the assistant!",
        .timestamp = 1234567890,
        .streaming = false
    };
    
    ConfigurableMockServer::MockResponse chat_response;
    chat_response.response = jsonrpc::JsonRpcResponseBuilder()
        .id(1)
        .result(chat_result)
        .buildJson();
    chat_response.once = true;
    server.QueueNextResponse(chat_response);
    
    // Create client and connect
    MCPClient client("127.0.0.1", port);
    
    std::atomic<bool> response_received{false};
    std::atomic<bool> id_received{false};
    std::string received_type;
    std::string received_content;
    int received_id = -1;
    
    client.SetResponseCallback([&](const std::string& type, const std::string& content) {
        received_type = type;
        received_content = content;
        response_received = true;
    });
    
    client.SetIdCallback([&](int id) {
        received_id = id;
        id_received = true;
    });
    
    if (!client.Connect()) {
        std::cerr << "Failed to connect to mock server" << std::endl;
        return false;
    }
    
    // Send a JSON-RPC request
    std::string request_json = jsonrpc::JsonRpcRequestBuilder()
        .method("chat.send")
        .id(1)
        .params(jsonrpc::ChatParams{
            .content = "Hello!",
            .model = "test"
        })
        .buildJson();
    
    client.SendRequest(request_json);
    
    // Wait for response
    for (int i = 0; i < 50 && !response_received; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Check what the server received
    auto received_msgs = server.GetReceivedMessages();
    if (!received_msgs.empty()) {
        std::cerr << "Server received: " << received_msgs[0] << std::endl;
    }
    
    if (!response_received) {
        std::cerr << "Did not receive response" << std::endl;
        return false;
    }
    
    if (!id_received) {
        std::cerr << "Did not receive ID callback" << std::endl;
        return false;
    }
    
    if (received_id != 1) {
        std::cerr << "Received wrong ID: " << received_id << std::endl;
        return false;
    }
    
    if (received_type != "RESPONSE") {
        std::cerr << "Expected type RESPONSE, got: " << received_type << std::endl;
        return false;
    }
    
    if (received_content != "Hello from the assistant!") {
        std::cerr << "Expected content 'Hello from the assistant!', got: " << received_content << std::endl;
        return false;
    }
    
    std::cout << "✅ JSON-RPC 2.0 chat response parsing works!" << std::endl;
    return true;
}

bool test_json_rpc_error_response() {
    std::cout << "Testing JSON-RPC 2.0 error response parsing..." << std::endl;
    
    // Start mock server
    ConfigurableMockServer server;
    int port = server.Start();
    
    // Queue a JSON-RPC error response
    ConfigurableMockServer::MockResponse error_response;
    error_response.response = jsonrpc::JsonRpcResponseBuilder()
        .id(2)
        .error(-32001, "Request timed out", "{\"canRetry\":true,\"originalMessage\":\"Test message\"}")
        .buildJson();
    error_response.once = true;
    server.QueueNextResponse(error_response);
    
    // Create client and connect
    MCPClient client("127.0.0.1", port);
    
    std::atomic<bool> response_received{false};
    std::atomic<bool> id_received{false};
    std::string received_type;
    std::string received_content;
    int received_id = -1;
    
    client.SetResponseCallback([&](const std::string& type, const std::string& content) {
        received_type = type;
        received_content = content;
        response_received = true;
    });
    
    client.SetIdCallback([&](int id) {
        received_id = id;
        id_received = true;
    });
    
    if (!client.Connect()) {
        std::cerr << "Failed to connect to mock server" << std::endl;
        return false;
    }
    
    // Send a JSON-RPC request
    std::string request_json = jsonrpc::JsonRpcRequestBuilder()
        .method("chat.send")
        .id(2)
        .params(jsonrpc::ChatParams{
            .content = "Test message",
            .model = "test"
        })
        .buildJson();
    
    client.SendRequest(request_json);
    
    // Wait for response
    for (int i = 0; i < 50 && !response_received; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Check what the server received
    auto received_msgs = server.GetReceivedMessages();
    if (!received_msgs.empty()) {
        std::cerr << "Server received: " << received_msgs[0] << std::endl;
    }
    
    if (!response_received) {
        std::cerr << "Did not receive response" << std::endl;
        return false;
    }
    
    if (!id_received) {
        std::cerr << "Did not receive ID callback" << std::endl;
        return false;
    }
    
    if (received_id != 2) {
        std::cerr << "Received wrong ID: " << received_id << std::endl;
        return false;
    }
    
    if (received_type != "TIMEOUT_WITH_RETRY") {
        std::cerr << "Expected type TIMEOUT_WITH_RETRY, got: " << received_type << std::endl;
        return false;
    }
    
    if (received_content.find("Request timed out") == std::string::npos) {
        std::cerr << "Expected timeout message in content, got: " << received_content << std::endl;
        return false;
    }
    
    std::cout << "✅ JSON-RPC 2.0 error response parsing works!" << std::endl;
    return true;
}

bool test_json_rpc_notification() {
    std::cout << "Testing JSON-RPC 2.0 notification parsing..." << std::endl;
    
    // For now, we'll skip testing notifications since the mock server
    // only sends responses to requests, not unsolicited notifications.
    // This would be tested with the real server that can send notifications.
    
    std::cout << "✅ JSON-RPC 2.0 notification parsing (skipped - needs real server)" << std::endl;
    return true;
}

int main() {
    std::cout << "Testing MCPClient JSON-RPC 2.0 support...\n" << std::endl;
    
    int passed = 0;
    int failed = 0;
    
    if (test_json_rpc_chat_response()) passed++; else failed++;
    if (test_json_rpc_error_response()) passed++; else failed++;
    if (test_json_rpc_notification()) passed++; else failed++;
    
    std::cout << "\n=== Test Results ===" << std::endl;
    std::cout << "Passed: " << passed << std::endl;
    std::cout << "Failed: " << failed << std::endl;
    
    return failed > 0 ? 1 : 0;
}