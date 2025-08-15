/**
 * Headless CLI - Simple command-line interface
 * 
 * Reads commands from stdin, sends to server, prints responses to stdout
 * No TUI, just a raw CLI interface for scripting and testing
 */

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <signal.h>
#include "core/headless_application.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

using namespace std::chrono_literals;

// Global for signal handling
std::atomic<bool> should_exit{false};
HeadlessApplication* g_app = nullptr;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        should_exit = true;
        if (g_app) {
            g_app->Stop();
        }
    }
}

void print_help() {
    std::cout << R"(
Headless CLI - MCP Bridge Client

Commands (all from InputHandler):
  /help      - Show this help
  /model <kimi|o3>  - Choose backend model
  /think <lvl>      - Set reasoning effort (minimal|low|medium|high)
  /tools     - List available tools
  /servers   - Show connected servers
  /clear     - Clear conversation history
  /list      - List saved conversations
  /load N    - Load conversation #N
  /delete N  - Delete conversation #N
  /new       - Start new chat conversation
  /sync      - Check chat history synchronization
  /exit      - Exit the application (aliases: /quit, /q)
  
Any other text will be sent as a chat message.
)" << std::endl;
}

int main(int argc, char* argv[]) {
    // Set up logging to file only
    auto logger = spdlog::basic_logger_mt("headless_cli", ".logs/headless_cli.log");
    spdlog::set_default_logger(logger);
    spdlog::set_level(spdlog::level::info);
    
    // Parse command line arguments
    std::string host = "127.0.0.1";
    int port = 4000;
    std::string config_file = "tests/test_config.json";
    
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --host <host>    Server host (default: 127.0.0.1)" << std::endl;
            std::cout << "  --port <port>    Server port (default: 4000)" << std::endl;
            std::cout << "  --config <file>  Config file (default: tests/test_config.json)" << std::endl;
            std::cout << "  --help           Show this help" << std::endl;
            return 0;
        }
    }
    
    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create and initialize application
    HeadlessApplication app;
    g_app = &app;
    
    if (!app.Initialize(config_file)) {
        std::cerr << "Failed to initialize application with config: " << config_file << std::endl;
        return 1;
    }
    
    // Set up message callback to print to stdout
    app.OnMessage([](LogEntryType type, const std::string& content) {
        switch (type) {
            case LogEntryType::USER:
                // Don't echo user input
                break;
            case LogEntryType::RESPONSE:
                std::cout << content << std::endl;
                break;
            case LogEntryType::ERROR:
                std::cerr << "Error: " << content << std::endl;
                break;
            case LogEntryType::SYSTEM:
                std::cout << "[System] " << content << std::endl;
                break;
            case LogEntryType::BLOCKED:
                std::cout << "[Blocked] " << content << std::endl;
                break;
        }
    });
    
    // Set up connection callback
    std::atomic<bool> connected{false};
    app.OnConnectionChange([&connected](bool is_connected) {
        connected = is_connected;
        if (is_connected) {
            std::cout << "Connected to server" << std::endl;
        } else {
            std::cout << "Disconnected from server" << std::endl;
        }
    });
    
    // Connect to server
    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;
    if (!app.Connect(host, port)) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    // Start the application
    app.Start();
    
    // Wait for connection
    if (!app.WaitForState([&connected]() { return connected.load(); }, 5s)) {
        std::cerr << "Connection timeout" << std::endl;
        return 1;
    }
    
    // Print welcome message
    std::cout << "Connected! Type /help for commands or enter a message." << std::endl;
    std::cout << "> " << std::flush;
    
    // Main input loop
    std::string line;
    while (!should_exit && std::getline(std::cin, line)) {
        if (line.empty()) {
            std::cout << "> " << std::flush;
            continue;
        }
        
        // Handle commands
        if (line[0] == '/') {
            // Special handling for exit commands only
            if (line == "/exit" || line == "/quit" || line == "/q") {
                std::cout << "Exiting..." << std::endl;
                break;
            }
            
            // Pass ALL other commands through to InputHandler::ProcessCommand
            // This gives us access to all commands: /model, /think, /tools, /new, /list, /load, etc.
            auto result = app.ExecuteCommand(line, 5s);
            if (!result.success && result.response_type == LogEntryType::ERROR) {
                // Only show error if it's actually an error
                if (!result.response.empty()) {
                    std::cerr << result.response << std::endl;
                }
            }
        } else {
            // Regular chat message
            std::cout << "Sending message..." << std::endl;
            auto result = app.SendChatMessage(line);  // Uses default timeout from procedure
            
            if (!result.success) {
                std::cerr << "Failed to send message: " << result.response << std::endl;
            }
            // Response will be printed by the callback
        }
        
        // Print prompt for next input
        if (!should_exit) {
            std::cout << "> " << std::flush;
        }
    }
    
    // Clean up
    std::cout << "Shutting down..." << std::endl;
    app.Stop();
    g_app = nullptr;
    
    return 0;
}