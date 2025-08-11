/*
 * Copyright 2025 Martin Boros
 * Licensed under the Apache License, Version 2.0
 */

#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <queue>

#include "../src/network/mcp_client.hpp"
#include "mock_tcp_server.hpp"

// Test that tool events are properly parsed and forwarded to the UI
void test_tool_event_handling() {
    std::cout << "Testing tool event handling..." << std::endl;
    
    // Create mock server
    MockTcpServer mock_server(8087);
    
    // Set up a queue of responses to cycle through
    std::queue<std::string> responses;
    responses.push("{\"type\":\"tool_call\",\"tool_name\":\"list_files\","
                   "\"arguments\":{\"base_path\":\"/test\",\"recursive\":true},"
                   "\"timestamp\":1234567890}");
    responses.push("{\"type\":\"tool_result_preview\",\"tool_name\":\"list_files\","
                   "\"preview\":\"  📁 src\\n  📄 README.md\\n  📄 package.json\","
                   "\"total_items\":25,\"timestamp\":1234567891}");
    responses.push("{\"type\":\"tool_error\",\"tool_name\":\"list_files\","
                   "\"error\":\"Permission denied: /private\",\"timestamp\":1234567892}");
    responses.push("{\"type\":\"tool_info\","
                   "\"message\":\"AI response without tools (1 available)\","
                   "\"timestamp\":1234567893}");
    responses.push("{\"type\":\"response\","
                   "\"reply\":\"Here is my response to your question.\","
                   "\"timestamp\":1234567894}");
    
    // Set up response callback to return tool events
    mock_server.SetResponseCallback([&responses](const std::string& request) {
        std::cout << "  Mock server received request: " << request << std::endl;
        if (!responses.empty()) {
            std::string response = responses.front();
            responses.pop();
            std::cout << "  Mock server sending response: " << response.substr(0, 80) << "..." << std::endl;
            return response;
        }
        std::cout << "  Mock server sending default ack" << std::endl;
        return std::string("{\"type\":\"ack\"}");
    });
    
    mock_server.Start();
    
    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create MCP client
    MCPClient client("127.0.0.1", 8087);
    
    // Track received callbacks
    std::vector<std::pair<std::string, std::string>> received_callbacks;
    
    // Set up callback to capture tool events
    client.SetResponseCallback([&received_callbacks](const std::string& type, const std::string& content) {
        received_callbacks.push_back({type, content});
        std::cout << "  Received callback: type=" << type << ", content=" << content << std::endl;
    });
    
    // Connect to mock server
    bool connected = client.Connect();
    assert(connected && "Client should connect to mock server");
    
    // Give connection time to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Test 1: Tool call event
    std::cout << "  Sending test1 request..." << std::endl;
    client.SendRequest("{\"type\":\"test1\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Give more time
    
    std::cout << "  Callbacks received so far: " << received_callbacks.size() << std::endl;
    for (const auto& cb : received_callbacks) {
        std::cout << "    - Type: " << cb.first << ", Content: " << cb.second.substr(0, 80) << "..." << std::endl;
    }
    
    assert(received_callbacks.size() >= 1 && "Should have received tool call event");
    assert(received_callbacks.back().first == "TOOL_EVENT" && "Should be TOOL_EVENT type");
    assert(received_callbacks.back().second.find("Tool call: list_files") != std::string::npos && 
           "Should contain tool name");
    assert(received_callbacks.back().second.find("recursive") != std::string::npos && 
           "Should contain arguments");
    
    // Test 2: Tool result preview event
    client.SendRequest("{\"type\":\"test2\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    assert(received_callbacks.size() >= 2 && "Should have received tool result preview");
    assert(received_callbacks.back().first == "TOOL_EVENT" && "Should be TOOL_EVENT type");
    assert(received_callbacks.back().second.find("Tool result for list_files") != std::string::npos && 
           "Should contain tool result header");
    assert(received_callbacks.back().second.find("README.md") != std::string::npos && 
           "Should contain preview content");
    assert(received_callbacks.back().second.find("Total items: 25") != std::string::npos && 
           "Should contain total items count");
    
    // Test 3: Tool error event
    client.SendRequest("{\"type\":\"test3\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    assert(received_callbacks.size() >= 3 && "Should have received tool error");
    assert(received_callbacks.back().first == "TOOL_EVENT" && "Should be TOOL_EVENT type");
    assert(received_callbacks.back().second.find("Tool error for list_files") != std::string::npos && 
           "Should contain error header");
    assert(received_callbacks.back().second.find("Permission denied") != std::string::npos && 
           "Should contain error message");
    
    // Test 4: Tool info event (when AI doesn't call tools)
    client.SendRequest("{\"type\":\"test4\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    assert(received_callbacks.size() >= 4 && "Should have received tool info");
    assert(received_callbacks.back().first == "TOOL_EVENT" && "Should be TOOL_EVENT type");
    assert(received_callbacks.back().second.find("AI response without tools") != std::string::npos && 
           "Should contain info message");
    
    // Test 5: Regular chat response (should not be TOOL_EVENT)
    client.SendRequest("{\"type\":\"test5\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    assert(received_callbacks.size() >= 5 && "Should have received chat response");
    assert(received_callbacks.back().first == "RESPONSE" && "Should be RESPONSE type, not TOOL_EVENT");
    assert(received_callbacks.back().second == "Here is my response to your question." && 
           "Should contain the response text");
    
    // Clean up
    client.Stop();
    mock_server.Stop();
    
    std::cout << "✓ Tool event handling test passed" << std::endl;
    std::cout << "  Total callbacks received: " << received_callbacks.size() << std::endl;
}

// Test that tool events with malformed JSON are handled gracefully
void test_malformed_tool_events() {
    std::cout << "Testing malformed tool event handling..." << std::endl;
    
    // Create mock server
    MockTcpServer mock_server(8088);
    
    // Set up responses with malformed/missing fields
    std::queue<std::string> responses;
    responses.push("{\"type\":\"tool_call\","
                   "\"arguments\":{\"base_path\":\"/test\"},"
                   "\"timestamp\":1234567890}");  // Missing tool_name
    responses.push("{\"type\":\"tool_result_preview\","
                   "\"tool_name\":\"test_tool\",\"preview\":\"\","
                   "\"total_items\":0,\"timestamp\":1234567891}");  // Empty preview
    
    mock_server.SetResponseCallback([&responses](const std::string& request) {
        if (!responses.empty()) {
            std::string response = responses.front();
            responses.pop();
            return response;
        }
        return std::string("{\"type\":\"ack\"}");
    });
    
    mock_server.Start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Create MCP client
    MCPClient client("127.0.0.1", 8088);
    
    // Track received callbacks
    std::vector<std::pair<std::string, std::string>> received_callbacks;
    
    client.SetResponseCallback([&received_callbacks](const std::string& type, const std::string& content) {
        received_callbacks.push_back({type, content});
    });
    
    // Connect to mock server
    bool connected = client.Connect();
    assert(connected && "Client should connect to mock server");
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Test with missing tool_name field
    client.SendRequest("{\"type\":\"test1\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should still handle it gracefully with "unknown" as tool name
    assert(received_callbacks.size() >= 1 && "Should have received event despite missing field");
    assert(received_callbacks.back().first == "TOOL_EVENT" && "Should be TOOL_EVENT type");
    assert(received_callbacks.back().second.find("Tool call: unknown") != std::string::npos && 
           "Should use 'unknown' for missing tool name");
    
    // Test with empty preview field
    client.SendRequest("{\"type\":\"test2\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Should handle empty preview gracefully
    assert(received_callbacks.size() >= 2 && "Should have received event with empty preview");
    assert(received_callbacks.back().first == "TOOL_EVENT" && "Should be TOOL_EVENT type");
    assert(received_callbacks.back().second.find("Tool result for test_tool") != std::string::npos && 
           "Should still show tool result header");
    
    // Clean up
    client.Stop();
    mock_server.Stop();
    
    std::cout << "✓ Malformed tool event handling test passed" << std::endl;
}

int main() {
    std::cout << "\n=== Testing Tool Event Handling ===" << std::endl;
    
    try {
        test_tool_event_handling();
        test_malformed_tool_events();
        
        std::cout << "\n✓ All tool event tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}