#pragma once

#include <string>
#include <variant>
#include <atomic_queue.h>

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

using AppEventQueue = atomic_queue::AtomicQueueB2<AppEvent>;