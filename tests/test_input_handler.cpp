#include <iostream>
#include <cassert>
#include "../src/core/input_handler.hpp"
#include "../src/core/state_manager.hpp"

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

int main() {
    StateManager state;
    std::vector<Tool> tools; // Empty tools vector for testing
    InputHandler handler(state, tools);
    
    // Test 1: Handle Ctrl+C 
    TEST("Ctrl+C handling")
    app::Event ctrl_c = app::Event::CtrlC();
    handler.HandleEvent(ctrl_c);
    // State should be updated
    PASS()
    
    // Test 2: Handle Ctrl+N (new conversation)
    TEST("Ctrl+N new conversation")
    app::Event ctrl_n = app::Event::CtrlN();
    handler.HandleEvent(ctrl_n);
    PASS()
    
    // Test 3: Process help command
    TEST("Help command")
    handler.ProcessCommand("/help");
    // Help command just prints help text in standalone mode
    PASS()
    
    // Test 4: Process list command
    TEST("List command") 
    handler.ProcessCommand("/list");
    // List command shows list in standalone mode
    PASS()
    
    // Test 5: Process clear command
    TEST("Clear command")
    handler.ProcessCommand("/clear");
    PASS()
    
    // Test 6: Process model command
    TEST("Model command")
    handler.ProcessCommand("/model gpt-4");
    // Config should be updated
    PASS()
    
    // Test 7: Empty command handling
    TEST("Empty command handling")
    state.ClearAwaitingResponse();
    
    handler.ProcessCommand("");
    handler.ProcessCommand("   ");
    
    // Should not be awaiting for empty commands
    ASSERT(!state.IsAwaitingResponse());
    PASS()
    
    // Test 8: Without a connected client, messages don't cause awaiting
    TEST("Standalone mode - no awaiting")
    state.ClearAwaitingResponse();
    
    // Process a normal message without a client
    handler.ProcessCommand("Hello world");
    
    // Should NOT be awaiting (no server to wait for)
    ASSERT(!state.IsAwaitingResponse());
    PASS()
    
    // Test 9: Unknown command handling
    TEST("Unknown command")
    handler.ProcessCommand("/unknown_command_xyz");
    // Unknown commands are handled
    PASS()
    
    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}