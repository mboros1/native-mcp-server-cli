# Event Abstraction Layer Usage Guide

## Overview

The event abstraction layer allows us to remove FTXUI dependencies from core business logic components, making them testable without UI frameworks.

## Architecture

```
┌─────────────────────────────────────────────────┐
│                Application Layer                 │
├─────────────────────────────────────────────────┤
│              app::Event Interface               │
├──────────────────┬──────────────────────────────┤
│   FTXUI Adapter  │      Headless Adapter        │
├──────────────────┼──────────────────────────────┤
│   FTXUI Events   │    Simulated Events          │
└──────────────────┴──────────────────────────────┘
```

## Components

### 1. `app::Event` (app_events.hpp)
- Framework-agnostic event representation
- Covers keyboard, mouse, and custom events
- Simple comparison operators for easy checking

### 2. `FTXUIEventAdapter` (ftxui_event_adapter.hpp)
- Converts FTXUI events to app events
- Isolates FTXUI dependency to this single file
- Used only in the UI application

### 3. Updated Components
- `InputHandler` - Now uses `app::Event` instead of `ftxui::Event`
- `ConversationLogManager` - Can be updated similarly
- `InputWithHistory` - Remains FTXUI-specific (UI component)

## Usage Examples

### In FTXUI Application (main.cpp)

```cpp
#include "adapters/ftxui_event_adapter.hpp"

class Application {
private:
    app::FTXUIEventAdapter event_adapter_;
    
public:
    void SetupEventHandlers() {
        // Wrap FTXUI event handler
        auto event_handler = CatchEvent(component, [this](ftxui::Event ftxui_event) {
            // Convert FTXUI event to app event
            app::Event event = event_adapter_.FromFTXUI(ftxui_event);
            
            // Pass to InputHandler (no FTXUI dependency)
            if (input_handler_->HandleEvent(event)) {
                return true;
            }
            
            // Handle other events...
            return false;
        });
    }
};
```

### In Headless Application

```cpp
class HeadlessApplication {
public:
    void SimulateKeyPress(char key) {
        // Create app event directly (no FTXUI)
        app::Event event = app::Event::Character(std::string(1, key));
        input_handler_->HandleEvent(event);
    }
    
    void SimulateCtrlC() {
        app::Event event = app::Event::CtrlC();
        input_handler_->HandleEvent(event);
    }
    
    void SimulateEscape() {
        app::Event event = app::Event::Escape();
        input_handler_->HandleEvent(event);
    }
};
```

### In Tests

```cpp
TEST(InputHandlerTest, HandleCtrlC) {
    StateManager state;
    std::vector<Tool> tools;
    InputHandler handler(state, tools);
    
    // Test Ctrl+C handling (no FTXUI needed)
    app::Event ctrl_c = app::Event::CtrlC();
    bool handled = handler.HandleEvent(ctrl_c);
    
    EXPECT_TRUE(handled);
    EXPECT_TRUE(state.IsCtrlCPending());
}

TEST(InputHandlerTest, HandleEscape) {
    StateManager state;
    std::vector<Tool> tools;
    InputHandler handler(state, tools);
    
    // Simulate awaiting response
    state.SetAwaitingResponse("test message");
    
    // Test Escape handling
    app::Event escape = app::Event::Escape();
    bool handled = handler.HandleEvent(escape);
    
    EXPECT_TRUE(handled);
    EXPECT_FALSE(state.IsAwaitingResponse());
}
```

## Migration Steps

### 1. Update InputHandler

```cpp
// Before (FTXUI dependency)
#include <ftxui/component/event.hpp>
bool HandleEvent(const ftxui::Event& event);

// After (no FTXUI dependency)
#include "../include/app_events.hpp"
bool HandleEvent(const app::Event& event);
```

### 2. Update ConversationLogManager

```cpp
// Before
bool HandleScrollEvent(ftxui::Event event) {
    if (event.is_mouse() && event.mouse().button == ftxui::Mouse::WheelUp) {
        // ...
    }
}

// After
bool HandleScrollEvent(const app::Event& event) {
    if (event == app::EventType::MouseWheelUp) {
        // ...
    }
}
```

### 3. Update Main Application

```cpp
// Add adapter
app::FTXUIEventAdapter event_adapter_;

// Convert events in handlers
app::Event event = event_adapter_.FromFTXUI(ftxui_event);
```

## Benefits

1. **Testability**: Core logic can be tested without UI framework
2. **Portability**: Easy to swap UI frameworks (Qt, ncurses, etc.)
3. **Simplicity**: Cleaner event handling code
4. **Performance**: No UI overhead in tests
5. **CI/CD**: Can run tests in headless environments

## Advanced Usage

### Custom Events

```cpp
// Define custom event type
enum class CustomEventType {
    DataReceived,
    ConnectionLost,
    TimerExpired
};

// Extend Event class if needed
class ExtendedEvent : public app::Event {
    // Add custom data
};
```

### Event Simulation for Testing

```cpp
class EventSimulator {
public:
    static std::vector<app::Event> TypeString(const std::string& text) {
        std::vector<app::Event> events;
        for (char c : text) {
            events.push_back(app::Event::Character(std::string(1, c)));
        }
        return events;
    }
    
    static std::vector<app::Event> NavigateUp(int times = 1) {
        std::vector<app::Event> events;
        for (int i = 0; i < times; i++) {
            events.push_back(app::Event::ArrowUp());
        }
        return events;
    }
};
```

## Conclusion

The event abstraction layer successfully decouples business logic from the UI framework, enabling comprehensive testing and future UI flexibility. The migration path is straightforward and can be done incrementally.