// Snippet showing the updated HandleEvent method with abstracted events

#include "input_handler_v2.hpp"

// ... (rest of the implementation stays the same) ...

bool InputHandler::HandleEvent(const app::Event& event) {
  // Handle Ctrl+C
  if (event == app::EventType::CtrlC) {
    state_.HandleCtrlC();
    return true;
  }
  
  // Handle Escape key as interrupt when awaiting response
  if (event == app::EventType::Escape && state_.IsAwaitingResponse()) {
    SendInterrupt();
    return true;
  }
  
  // Reset Ctrl+C and Esc on other input
  if (event.is_character()) {
    state_.ResetCtrlC();
    state_.ResetEsc();
  }
  
  // Ctrl+N handling is done in the Application's input component wrapper
  // Not here in InputHandler
  
  return false;
}