#ifndef APP_EVENTS_HPP
#define APP_EVENTS_HPP

#include <string>
#include <variant>
#include <functional>

/**
 * Application-level event abstraction
 * This provides a UI-framework-agnostic event system that can be 
 * implemented by different frontends (FTXUI, headless, etc.)
 */

namespace app {

// Event types that our application cares about
enum class EventType {
    // Keyboard events
    Character,      // Regular character input
    CtrlC,         // Ctrl+C pressed
    CtrlN,         // Ctrl+N pressed (send)
    Escape,        // Escape key
    Enter,         // Enter key
    Backspace,     // Backspace key
    Delete,        // Delete key
    
    // Navigation events
    ArrowUp,       // Up arrow / history previous
    ArrowDown,     // Down arrow / history next
    ArrowLeft,     // Left arrow
    ArrowRight,    // Right arrow
    PageUp,        // Page up
    PageDown,      // Page down
    Home,          // Home key
    End,           // End key
    
    // Mouse events (simplified)
    MouseWheelUp,  // Scroll up
    MouseWheelDown,// Scroll down
    MouseClick,    // Click at position
    
    // Custom application events
    Custom,        // Custom/refresh event
    None           // No event / unknown
};

// Mouse information for click events
struct MouseInfo {
    int x;
    int y;
    int button; // 0=left, 1=middle, 2=right
};

// Character information
struct CharInfo {
    std::string character;  // Can be multi-byte for Unicode
    bool is_printable;
};

// Unified event structure
class Event {
private:
    EventType type_;
    std::variant<std::monostate, CharInfo, MouseInfo> data_;
    
public:
    Event() : type_(EventType::None) {}
    explicit Event(EventType type) : type_(type) {}
    Event(EventType type, CharInfo info) : type_(type), data_(info) {}
    Event(EventType type, MouseInfo info) : type_(type), data_(info) {}
    
    EventType type() const { return type_; }
    
    bool is_character() const { 
        return type_ == EventType::Character; 
    }
    
    bool is_control() const {
        return type_ == EventType::CtrlC || 
               type_ == EventType::CtrlN ||
               type_ == EventType::Escape ||
               type_ == EventType::Enter ||
               type_ == EventType::Backspace ||
               type_ == EventType::Delete;
    }
    
    bool is_navigation() const {
        return type_ == EventType::ArrowUp ||
               type_ == EventType::ArrowDown ||
               type_ == EventType::ArrowLeft ||
               type_ == EventType::ArrowRight ||
               type_ == EventType::PageUp ||
               type_ == EventType::PageDown ||
               type_ == EventType::Home ||
               type_ == EventType::End;
    }
    
    bool is_mouse() const {
        return type_ == EventType::MouseWheelUp ||
               type_ == EventType::MouseWheelDown ||
               type_ == EventType::MouseClick;
    }
    
    // Get character data if this is a character event
    std::string character() const {
        if (type_ == EventType::Character) {
            if (auto* info = std::get_if<CharInfo>(&data_)) {
                return info->character;
            }
        }
        return "";
    }
    
    // Get mouse info if this is a mouse event
    MouseInfo mouse() const {
        if (is_mouse()) {
            if (auto* info = std::get_if<MouseInfo>(&data_)) {
                return *info;
            }
        }
        return MouseInfo{0, 0, 0};
    }
    
    // Comparison operators for easy checking
    bool operator==(EventType type) const { return type_ == type; }
    bool operator!=(EventType type) const { return type_ != type; }
    
    // Static factory methods for common events
    static Event Character(const std::string& ch) {
        return Event(EventType::Character, CharInfo{ch, true});
    }
    
    static Event CtrlC() { return Event(EventType::CtrlC); }
    static Event CtrlN() { return Event(EventType::CtrlN); }
    static Event Escape() { return Event(EventType::Escape); }
    static Event Enter() { return Event(EventType::Enter); }
    static Event ArrowUp() { return Event(EventType::ArrowUp); }
    static Event ArrowDown() { return Event(EventType::ArrowDown); }
    static Event Custom() { return Event(EventType::Custom); }
};

// Event handler interface
using EventHandler = std::function<bool(const Event&)>;

// Interface for event adapters (to be implemented by UI frameworks)
class IEventAdapter {
public:
    virtual ~IEventAdapter() = default;
    
    // Convert from framework-specific event to app event
    virtual Event FromFramework(void* framework_event) = 0;
    
    // Convert from app event to framework-specific event (if needed)
    virtual void* ToFramework(const Event& event) = 0;
};

} // namespace app

#endif // APP_EVENTS_HPP