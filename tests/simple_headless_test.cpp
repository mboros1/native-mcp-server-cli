/**
 * Simple test to verify headless compilation works
 */

#include "../src/core/state_manager.hpp"
#include "../src/core/input_handler.hpp"
#include "../src/include/app_events.hpp"
#include <iostream>
#include <cassert>

int main() {
    std::cout << "Running simple headless test...\n\n";
    
    // Test 1: StateManager (no UI dependencies)
    {
        StateManager state;
        state.AddLogEntry(LogEntryType::SYSTEM, "Test message");
        auto log = state.GetEventLog();
        assert(!log.empty());
        assert(log.back().content == "Test message");
        std::cout << "✓ StateManager works\n";
    }
    
    // Test 2: InputHandler with app::Event
    {
        StateManager state;
        std::vector<Tool> tools;
        InputHandler handler(state, tools);
        
        // Test event handling with abstracted events
        app::Event ctrl_c = app::Event::CtrlC();
        bool handled = handler.HandleEvent(ctrl_c);
        assert(handled);
        assert(state.IsCtrlCPending());
        std::cout << "✓ InputHandler event handling works\n";
    }
    
    // Test 3: Command processing
    {
        StateManager state;
        std::vector<Tool> tools;
        InputHandler handler(state, tools);
        
        // Process a help command
        handler.ProcessCommand("/help");
        
        // Check that something was logged
        auto log = state.GetEventLog();
        assert(log.size() >= 2); // User input + system response
        std::cout << "✓ Command processing works\n";
        
        // Print what was logged
        std::cout << "\nLogged entries:\n";
        for (const auto& entry : log) {
            std::cout << "  - " << entry.content.substr(0, 50) 
                      << (entry.content.length() > 50 ? "..." : "") << "\n";
        }
    }
    
    // Test 4: Event abstraction
    {
        // Test creating various events without FTXUI
        auto char_event = app::Event::Character("a");
        assert(char_event.is_character());
        assert(char_event.character() == "a");
        
        auto nav_event = app::Event::ArrowUp();
        assert(nav_event.is_navigation());
        
        auto ctrl_event = app::Event::CtrlN();
        assert(ctrl_event.is_control());
        
        std::cout << "✓ Event abstraction works\n";
    }
    
    std::cout << "\n✅ All simple tests passed!\n";
    std::cout << "\nThis proves:\n";
    std::cout << "  - Core logic compiles without FTXUI\n";
    std::cout << "  - Event abstraction works\n";
    std::cout << "  - StateManager and InputHandler are UI-independent\n";
    std::cout << "  - Headless testing is possible\n";
    
    return 0;
}