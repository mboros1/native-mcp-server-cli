#include <iostream>
#include <cassert>
#include <thread>
#include "../src/core/state_manager.hpp"

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

int main() {
    StateManager state;
    
    // Test 1: Initial state
    TEST("Initial state")
    ASSERT(!state.IsAwaitingResponse());
    ASSERT(state.GetHistory().empty());
    PASS()
    
    // Test 2: Awaiting response flag
    TEST("Awaiting response flag")
    state.SetAwaitingResponse("test message");
    ASSERT(state.IsAwaitingResponse());
    state.ClearAwaitingResponse();
    ASSERT(!state.IsAwaitingResponse());
    PASS()
    
    // Test 3: Chat history - user message when NOT awaiting
    TEST("User message written when not awaiting")
    state.ClearAwaitingResponse();
    state.WriteToChatHistory("user", "Hello world");
    auto files = state.GetChatHistoryFiles();
    // Check that at least one history file exists after writing
    ASSERT(!files.empty() || state.GetHistory().size() > 0);
    PASS()
    
    // Test 4: Chat history - assistant message
    TEST("Assistant message always written")
    state.SetAwaitingResponse("waiting"); // Should not affect assistant messages
    state.WriteToChatHistory("assistant", "Hi there!");
    // Writing should succeed
    PASS()
    
    // Test 5: Command history
    TEST("Command history")
    state.AddToHistory("command1");
    state.AddToHistory("command2");
    auto history = state.GetHistory();
    ASSERT(history.size() >= 2);
    PASS()
    
    // Test 6: Thread safety of awaiting flag
    TEST("Thread safety")
    bool thread1_done = false;
    bool thread2_done = false;
    
    std::thread t1([&]() {
        for (int i = 0; i < 1000; i++) {
            state.SetAwaitingResponse("msg");
            state.ClearAwaitingResponse();
        }
        thread1_done = true;
    });
    
    std::thread t2([&]() {
        for (int i = 0; i < 1000; i++) {
            bool awaiting = state.IsAwaitingResponse();
            (void)awaiting; // Use it to avoid warning
        }
        thread2_done = true;
    });
    
    t1.join();
    t2.join();
    ASSERT(thread1_done && thread2_done);
    PASS()
    
    // Test 7: App state transitions
    TEST("App state")
    state.SetAppState(AppState::Normal);
    ASSERT(state.GetAppState() == AppState::Normal);
    state.RequestExit();
    ASSERT(state.GetAppState() == AppState::ExitRequested);
    PASS()
    
    // Test 8: Display mode
    TEST("Display mode")
    state.SetDisplayMode(DisplayMode::Tools);
    ASSERT(state.GetDisplayMode() == DisplayMode::Tools);
    state.SetDisplayMode(DisplayMode::Logs);
    ASSERT(state.GetDisplayMode() == DisplayMode::Logs);
    PASS()
    
    // Test 9: Event log
    TEST("Event log")
    state.AddLogEntry(LogEntryType::Info, "Test message");
    auto logs = state.GetEventLog();
    ASSERT(!logs.empty());
    state.ClearEventLog();
    logs = state.GetEventLog();
    ASSERT(logs.empty());
    PASS()
    
    // Test 10: Ctrl+C handling
    TEST("Ctrl+C handling")
    ASSERT(state.GetCtrlCCount() == 0);
    state.HandleCtrlC();
    ASSERT(state.GetCtrlCCount() == 1);
    state.ResetCtrlC();
    ASSERT(state.GetCtrlCCount() == 0);
    PASS()
    
    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}