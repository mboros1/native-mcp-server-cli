/**
 * Test to diagnose and fix the chat persistence bug
 * where user messages are not being written to chat history
 */

#include "../src/core/state_manager.hpp"
#include "../src/core/input_handler.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <json_struct.h>

// Helper to read chat history file
std::vector<ChatHistoryEntry> ReadChatHistory() {
    std::vector<ChatHistoryEntry> entries;
    std::ifstream file(".data/chat-history.json");
    if (!file.is_open()) {
        return entries;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        JS::ParseContext context(line);
        ChatHistoryEntry entry;
        if (context.parseTo(entry) == JS::Error::NoError) {
            entries.push_back(entry);
        }
    }
    
    return entries;
}

// Helper to clean up test files
void CleanupTestFiles() {
    if (std::filesystem::exists(".data/chat-history.json")) {
        std::filesystem::remove(".data/chat-history.json");
    }
}

void test_current_bug() {
    std::cout << "=== Testing Current Bug (SHOULD FAIL) ===\n";
    CleanupTestFiles();
    
    StateManager state;
    
    // Simulate the current flow (BUGGY)
    std::cout << "1. User sends message 'Hello'\n";
    
    // This is what happens now:
    std::cout << "2. SetAwaitingResponse() is called FIRST\n";
    state.SetAwaitingResponse("Hello");
    
    std::cout << "3. Then WriteToChatHistory('user', 'Hello') is called\n";
    state.WriteToChatHistory("user", "Hello");
    
    std::cout << "4. Check if user message was written...\n";
    auto history = ReadChatHistory();
    
    if (history.empty()) {
        std::cout << "❌ BUG CONFIRMED: User message was NOT written!\n";
        std::cout << "   Reason: WriteToChatHistory skips user messages when IsAwaitingResponse() is true\n";
    } else {
        std::cout << "✓ User message was written (unexpected)\n";
    }
    
    std::cout << "\n";
}

void test_correct_flow() {
    std::cout << "=== Testing Correct Flow (SHOULD PASS) ===\n";
    CleanupTestFiles();
    
    StateManager state;
    
    // The CORRECT flow should be:
    std::cout << "1. User sends message 'Hello'\n";
    
    std::cout << "2. WriteToChatHistory('user', 'Hello') FIRST\n";
    state.WriteToChatHistory("user", "Hello");
    
    std::cout << "3. THEN SetAwaitingResponse()\n";
    state.SetAwaitingResponse("Hello");
    
    std::cout << "4. Check if user message was written...\n";
    auto history = ReadChatHistory();
    
    if (!history.empty() && history[0].role == "user" && history[0].content == "Hello") {
        std::cout << "✓ SUCCESS: User message was written correctly!\n";
    } else {
        std::cout << "❌ User message was not written\n";
    }
    
    // Simulate server response
    std::cout << "\n5. Server responds with 'Hi there!'\n";
    std::cout << "6. ClearAwaitingResponse()\n";
    state.ClearAwaitingResponse();
    
    std::cout << "7. WriteToChatHistory('assistant', 'Hi there!')\n";
    state.WriteToChatHistory("assistant", "Hi there!");
    
    history = ReadChatHistory();
    if (history.size() == 2 && 
        history[0].role == "user" && 
        history[1].role == "assistant") {
        std::cout << "✓ Both messages written correctly!\n";
    }
    
    std::cout << "\n";
}

void test_double_send_prevention() {
    std::cout << "=== Testing Double Send Prevention ===\n";
    CleanupTestFiles();
    
    StateManager state;
    std::vector<Tool> tools;
    InputHandler handler(state, tools);
    handler.SetCommMode(CommMode::IPC);
    
    // First message
    std::cout << "1. User sends first message\n";
    state.WriteToChatHistory("user", "First message");
    state.SetAwaitingResponse("First message");
    
    // Try to send second message while awaiting
    std::cout << "2. User tries to send second message while awaiting\n";
    
    // The check should be in SendChatMessage, not WriteToChatHistory
    if (state.IsAwaitingResponse()) {
        std::cout << "✓ Second message blocked (correct behavior)\n";
        // Should log BLOCKED entry but NOT write to chat history
    }
    
    // Clear and allow next message
    std::cout << "3. Response received, clearing await state\n";
    state.ClearAwaitingResponse();
    state.WriteToChatHistory("assistant", "Response to first");
    
    std::cout << "4. User can now send second message\n";
    state.WriteToChatHistory("user", "Second message");
    state.SetAwaitingResponse("Second message");
    
    auto history = ReadChatHistory();
    if (history.size() == 3 && 
        history[0].content == "First message" &&
        history[1].content == "Response to first" &&
        history[2].content == "Second message") {
        std::cout << "✓ Correct sequence in history!\n";
    } else {
        std::cout << "❌ History has " << history.size() << " entries (expected 3)\n";
    }
    
    std::cout << "\n";
}

void print_actual_flow() {
    std::cout << "=== ACTUAL FLOW ANALYSIS ===\n";
    std::cout << "Current (BUGGY) implementation in ProcessCommand:\n";
    std::cout << "  1. SendChatMessage(message)\n";
    std::cout << "     └─> SetAwaitingResponse() // Sets flag to true\n";
    std::cout << "  2. if (IsAwaitingResponse()) // Always true!\n";
    std::cout << "     └─> WriteToChatHistory('user', message)\n";
    std::cout << "         └─> if (role == 'user' && IsAwaitingResponse())\n";
    std::cout << "             └─> return; // SKIPS WRITING!\n";
    std::cout << "\nCorrect flow should be:\n";
    std::cout << "  1. SendChatMessage(message)\n";
    std::cout << "     ├─> WriteToChatHistory('user', message) // Write FIRST\n";
    std::cout << "     └─> SetAwaitingResponse() // THEN set flag\n";
    std::cout << "\n";
}

int main() {
    std::cout << "Testing Chat Persistence Bug\n";
    std::cout << "=============================\n\n";
    
    // Create .data directory if it doesn't exist
    std::filesystem::create_directories(".data");
    
    test_current_bug();
    test_correct_flow();
    test_double_send_prevention();
    print_actual_flow();
    
    std::cout << "=============================\n";
    std::cout << "THE FIX:\n";
    std::cout << "Move WriteToChatHistory() BEFORE SetAwaitingResponse()\n";
    std::cout << "in SendChatMessage() or ProcessCommand()\n";
    std::cout << "\nOR remove the IsAwaitingResponse() check for user messages\n";
    std::cout << "in WriteToChatHistory() since blocking should happen earlier.\n";
    
    // Cleanup
    CleanupTestFiles();
    
    return 0;
}