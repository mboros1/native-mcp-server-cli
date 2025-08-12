/**
 * Integration tests for JSON-RPC 2.0 communication
 * Tests the full stack: C++ client -> Node.js JSON-RPC server
 */

#include "../src/core/headless_application.hpp"
#include "../src/protocol/jsonrpc_procedures.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <cassert>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <filesystem>

using namespace std::chrono_literals;

// Helper to find config file from various locations
std::string FindTestConfigPath() {
    std::vector<std::string> paths = {
        "tests/test_config.json",
        "../tests/test_config.json",
        "./test_config.json",
        "test_config.json"
    };
    
    for (const auto& path : paths) {
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    
    // Default if not found
    return "tests/test_config.json";
}

class NodeServerProcess {
private:
    pid_t server_pid = -1;
    int server_port = 4567;  // Use non-default port for testing
    
public:
    NodeServerProcess() = default;
    
    ~NodeServerProcess() {
        Stop();
    }
    
    bool Start() {
        std::cout << "Starting Node.js JSON-RPC server..." << std::endl;
        
        server_pid = fork();
        if (server_pid == 0) {
            // Child process - run Node.js server
            // Set environment in child
            setenv("MCP_SERVER_PORT", std::to_string(server_port).c_str(), 1);
            
            // Redirect output to log files  
            freopen("../.logs/test-server-stdout.log", "w", stdout);
            freopen("../.logs/test-server-stderr.log", "w", stderr);
            
            // Change to project root directory
            chdir("..");
            
            // Execute Node.js server
            execl("/usr/bin/env", "env", "node", 
                  "server/mcp-bridge-server-jsonrpc.js", 
                  nullptr);
            
            // If execl fails
            std::cerr << "Failed to start Node.js server" << std::endl;
            exit(1);
        } else if (server_pid < 0) {
            std::cerr << "Failed to fork process" << std::endl;
            return false;
        }
        
        // Parent process - wait for server to start
        std::this_thread::sleep_for(2s);  // Give server time to initialize
        
        // Check if server is still running
        int status;
        pid_t result = waitpid(server_pid, &status, WNOHANG);
        if (result != 0) {
            std::cerr << "Server process died immediately" << std::endl;
            server_pid = -1;
            return false;
        }
        
        std::cout << "Node.js server started on port " << server_port << std::endl;
        return true;
    }
    
    void Stop() {
        if (server_pid > 0) {
            std::cout << "Stopping Node.js server..." << std::endl;
            kill(server_pid, SIGTERM);
            
            // Wait for process to terminate
            int status;
            waitpid(server_pid, &status, 0);
            
            server_pid = -1;
            std::cout << "Node.js server stopped" << std::endl;
        }
    }
    
    int GetPort() const { return server_port; }
};

void test_jsonrpc_hello() {
    std::cout << "\n=== Testing JSON-RPC Hello Procedure ===" << std::endl;
    
    NodeServerProcess server;
    if (!server.Start()) {
        std::cerr << "❌ Failed to start server" << std::endl;
        return;
    }
    
    // Create HeadlessApplication
    HeadlessApplication app;
    
    // Initialize with config
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication" << std::endl;
        return;
    }
    
    // Track responses
    std::atomic<bool> got_response{false};
    std::string last_response;
    
    app.OnMessage([&](LogEntryType type, const std::string& content) {
        if (type == LogEntryType::RESPONSE || type == LogEntryType::SYSTEM) {
            last_response = content;
            got_response = true;
            SPDLOG_INFO("Got response: {}", content);
        }
    });
    
    // Connect to server
    if (!app.Connect("127.0.0.1", server.GetPort())) {
        std::cerr << "❌ Failed to connect to server" << std::endl;
        return;
    }
    
    app.Start();
    
    // Wait for connection
    std::this_thread::sleep_for(1s);
    
    // Send hello command (will be converted to JSON-RPC)
    std::cout << "Sending hello request..." << std::endl;
    
    // The hello procedure should be called automatically on connection
    // Or we can trigger it via a command
    
    // For now, test basic chat functionality which uses JSON-RPC internally
    auto result = app.SendChatMessage("Hello, JSON-RPC server!", 5s);
    
    if (!result.success) {
        std::cerr << "❌ Failed to send message: " << result.response << std::endl;
    } else {
        std::cout << "✅ Got response: " << result.response << std::endl;
    }
    
    // Clean up
    app.Stop();
    server.Stop();
}

void test_jsonrpc_chat() {
    std::cout << "\n=== Testing JSON-RPC Chat Procedure ===" << std::endl;
    
    NodeServerProcess server;
    if (!server.Start()) {
        std::cerr << "❌ Failed to start server" << std::endl;
        return;
    }
    
    HeadlessApplication app;
    
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication" << std::endl;
        return;
    }
    
    // Connect to server
    if (!app.Connect("127.0.0.1", server.GetPort())) {
        std::cerr << "❌ Failed to connect to server" << std::endl;
        return;
    }
    
    app.Start();
    std::this_thread::sleep_for(1s);
    
    // Test chat.send procedure
    std::cout << "Testing chat.send..." << std::endl;
    auto result = app.SendChatMessage("What is 2+2?", 10s);
    
    if (!result.success) {
        std::cerr << "❌ Chat failed: " << result.response << std::endl;
        return;
    }
    
    std::cout << "✅ Chat response: " << result.response << std::endl;
    
    // Test conversation history
    auto log = app.GetConversationLog();
    std::cout << "Conversation has " << log.size() << " entries" << std::endl;
    
    // Send another message to test history context
    std::cout << "Testing follow-up message..." << std::endl;
    auto result2 = app.SendChatMessage("What was my previous question?", 10s);
    
    if (result2.success) {
        std::cout << "✅ Follow-up response: " << result2.response << std::endl;
    }
    
    // Clean up
    app.Stop();
    server.Stop();
}

void test_jsonrpc_tools() {
    std::cout << "\n=== Testing JSON-RPC Tool Procedures ===" << std::endl;
    
    NodeServerProcess server;
    if (!server.Start()) {
        std::cerr << "❌ Failed to start server" << std::endl;
        return;
    }
    
    HeadlessApplication app;
    
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication" << std::endl;
        return;
    }
    
    if (!app.Connect("127.0.0.1", server.GetPort())) {
        std::cerr << "❌ Failed to connect to server" << std::endl;
        return;
    }
    
    app.Start();
    std::this_thread::sleep_for(1s);
    
    // Test tools.list via /tools command
    std::cout << "Testing /tools command..." << std::endl;
    auto result = app.ExecuteCommand("/tools", 5s);
    
    if (!result.success) {
        std::cerr << "❌ Tools command failed: " << result.response << std::endl;
    } else {
        std::cout << "✅ Available tools: " << result.response << std::endl;
    }
    
    // Clean up
    app.Stop();
    server.Stop();
}

void test_jsonrpc_history() {
    std::cout << "\n=== Testing JSON-RPC History Procedures ===" << std::endl;
    
    NodeServerProcess server;
    if (!server.Start()) {
        std::cerr << "❌ Failed to start server" << std::endl;
        return;
    }
    
    HeadlessApplication app;
    
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication" << std::endl;
        return;
    }
    
    if (!app.Connect("127.0.0.1", server.GetPort())) {
        std::cerr << "❌ Failed to connect to server" << std::endl;
        return;
    }
    
    app.Start();
    std::this_thread::sleep_for(1s);
    
    // Send some messages to build history
    std::cout << "Building conversation history..." << std::endl;
    app.SendChatMessage("First message", 5s);
    app.SendChatMessage("Second message", 5s);
    
    // Test /new command (history.rotate)
    std::cout << "Testing /new command..." << std::endl;
    auto result = app.ExecuteCommand("/new", 5s);
    
    if (!result.success) {
        std::cerr << "❌ /new command failed: " << result.response << std::endl;
    } else {
        std::cout << "✅ History rotated: " << result.response << std::endl;
    }
    
    // Verify history was cleared
    auto log = app.GetConversationLog();
    std::cout << "After rotation, conversation has " << log.size() << " entries" << std::endl;
    
    // Clean up
    app.Stop();
    server.Stop();
}

void test_jsonrpc_error_handling() {
    std::cout << "\n=== Testing JSON-RPC Error Handling ===" << std::endl;
    
    NodeServerProcess server;
    if (!server.Start()) {
        std::cerr << "❌ Failed to start server" << std::endl;
        return;
    }
    
    HeadlessApplication app;
    
    std::string config_path = FindTestConfigPath();
    if (!app.Initialize(config_path)) {
        std::cerr << "❌ Failed to initialize HeadlessApplication" << std::endl;
        return;
    }
    
    if (!app.Connect("127.0.0.1", server.GetPort())) {
        std::cerr << "❌ Failed to connect to server" << std::endl;
        return;
    }
    
    app.Start();
    std::this_thread::sleep_for(1s);
    
    // Test with invalid model (if API key not set)
    std::cout << "Testing with potentially invalid configuration..." << std::endl;
    
    // Send a very short timeout message to trigger timeout
    std::cout << "Testing timeout handling..." << std::endl;
    auto result = app.SendChatMessage("This should timeout quickly", 100ms);
    
    if (!result.success) {
        std::cout << "✅ Got expected error/timeout: " << result.response << std::endl;
    } else {
        std::cout << "Response received (API may be very fast): " << result.response << std::endl;
    }
    
    // Clean up
    app.Stop();
    server.Stop();
}

int main() {
    spdlog::set_level(spdlog::level::info);
    std::cout << "=== JSON-RPC Integration Tests ===" << std::endl;
    std::cout << "Testing C++ client with Node.js JSON-RPC server" << std::endl;
    
    // Ensure logs directory exists
    std::filesystem::create_directories(".logs");
    
    try {
        test_jsonrpc_hello();
        test_jsonrpc_chat();
        test_jsonrpc_tools();
        test_jsonrpc_history();
        test_jsonrpc_error_handling();
        
        std::cout << "\n=== All Integration Tests Completed ===" << std::endl;
        std::cout << "Check .logs/test-server-*.log for server output" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}