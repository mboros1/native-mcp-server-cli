#ifndef FTXUI_EVENT_ADAPTER_HPP
#define FTXUI_EVENT_ADAPTER_HPP

#include "../include/app_events.hpp"
#include <ftxui/component/event.hpp>
#include <memory>

namespace app {

/**
 * Adapter to convert between FTXUI events and application events
 * This isolates FTXUI dependencies to this single adapter class
 */
class FTXUIEventAdapter : public IEventAdapter {
public:
    // Convert FTXUI event to app event
    Event FromFramework(void* framework_event) override {
        if (!framework_event) return Event(EventType::None);
        
        auto* ftxui_event = static_cast<ftxui::Event*>(framework_event);
        return Convert(*ftxui_event);
    }
    
    // Convert FTXUI event to app event (type-safe version)
    static Event Convert(ftxui::Event& ftxui_event) {
        // Control keys
        if (ftxui_event == ftxui::Event::CtrlC) {
            return Event::CtrlC();
        }
        if (ftxui_event == ftxui::Event::CtrlN) {
            return Event::CtrlN();
        }
        if (ftxui_event == ftxui::Event::Escape) {
            return Event::Escape();
        }
        if (ftxui_event == ftxui::Event::Return) {
            return Event::Enter();
        }
        if (ftxui_event == ftxui::Event::Backspace) {
            return Event(EventType::Backspace);
        }
        if (ftxui_event == ftxui::Event::Delete) {
            return Event(EventType::Delete);
        }
        
        // Navigation keys
        if (ftxui_event == ftxui::Event::ArrowUp) {
            return Event::ArrowUp();
        }
        if (ftxui_event == ftxui::Event::ArrowDown) {
            return Event::ArrowDown();
        }
        if (ftxui_event == ftxui::Event::ArrowLeft) {
            return Event(EventType::ArrowLeft);
        }
        if (ftxui_event == ftxui::Event::ArrowRight) {
            return Event(EventType::ArrowRight);
        }
        if (ftxui_event == ftxui::Event::PageUp) {
            return Event(EventType::PageUp);
        }
        if (ftxui_event == ftxui::Event::PageDown) {
            return Event(EventType::PageDown);
        }
        if (ftxui_event == ftxui::Event::Home) {
            return Event(EventType::Home);
        }
        if (ftxui_event == ftxui::Event::End) {
            return Event(EventType::End);
        }
        
        // Mouse events
        if (ftxui_event.is_mouse()) {
            if (ftxui_event.mouse().button == ftxui::Mouse::WheelUp) {
                return Event(EventType::MouseWheelUp);
            }
            if (ftxui_event.mouse().button == ftxui::Mouse::WheelDown) {
                return Event(EventType::MouseWheelDown);
            }
            if (ftxui_event.mouse().button == ftxui::Mouse::Left) {
                MouseInfo info{
                    ftxui_event.mouse().x,
                    ftxui_event.mouse().y,
                    0 // left button
                };
                return Event(EventType::MouseClick, info);
            }
        }
        
        // Character input
        if (ftxui_event.is_character()) {
            return Event::Character(ftxui_event.character());
        }
        
        // Custom event
        if (ftxui_event == ftxui::Event::Custom) {
            return Event::Custom();
        }
        
        // Unknown/unhandled
        return Event(EventType::None);
    }
    
    // Convert app event back to FTXUI event (if needed)
    void* ToFramework(const Event& event) override {
        // This would require creating FTXUI events, which is more complex
        // For now, we mainly need FromFramework for input handling
        // This could be implemented if bidirectional conversion is needed
        return nullptr;
    }
    
    // Helper to create FTXUI event for posting (e.g., refresh)
    static ftxui::Event ToFTXUIEvent(const Event& event) {
        switch (event.type()) {
            case EventType::Custom:
                return ftxui::Event::Custom;
            case EventType::CtrlC:
                return ftxui::Event::CtrlC;
            case EventType::CtrlN:
                return ftxui::Event::CtrlN;
            case EventType::Escape:
                return ftxui::Event::Escape;
            case EventType::Enter:
                return ftxui::Event::Return;
            case EventType::ArrowUp:
                return ftxui::Event::ArrowUp;
            case EventType::ArrowDown:
                return ftxui::Event::ArrowDown;
            // Add more as needed
            default:
                return ftxui::Event::Custom;
        }
    }
};

} // namespace app

#endif // FTXUI_EVENT_ADAPTER_HPP