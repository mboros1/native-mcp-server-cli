#pragma once

#include <string>
#include <variant>

enum class EventType {
    // Connection events
    Connected,
    ConnectionFailed,
    ConnectionLost,
    
    // Message events
    MessageReceived,
    MessageSent,
    
    // UI events
    InputReceived,
    RefreshRequired,
    
    // Application events
    Quit
};

struct AppEvent {
    EventType type;
    std::string data;
};

using AppEventQueue = ConcurrentQueue<AppEvent>;