#include "conversation_log_manager.hpp"

void ConversationLogManager::SetScreen(ScreenInteractive* screen) {
  screen_ = screen;
}

void ConversationLogManager::OnNewMessage() {
  // Called when a new message is added
  if (auto_scroll_) {
    scroll_y_ = 1.0f;  // Scroll to bottom
  }
  
  // Trigger screen refresh to immediately show new message
  if (screen_) {
    screen_->Post(Event::Custom);
  }
}

bool ConversationLogManager::HandleScrollEvent(Event event) {
  // Handle mouse wheel scrolling
  if (event.is_mouse() && event.mouse().button == Mouse::WheelUp) {
    scroll_y_ = std::max(0.0f, scroll_y_ - 0.1f);  // Scroll up
    auto_scroll_ = (scroll_y_ >= 0.99f);  // Re-enable auto-scroll if at bottom
    return true;
  }
  
  if (event.is_mouse() && event.mouse().button == Mouse::WheelDown) {
    scroll_y_ = std::min(1.0f, scroll_y_ + 0.1f);  // Scroll down
    auto_scroll_ = (scroll_y_ >= 0.99f);  // Re-enable auto-scroll if at bottom
    return true;
  }
  
  // Handle keyboard scrolling
  if (event == Event::ArrowUp || event == Event::PageUp) {
    scroll_y_ = std::max(0.0f, scroll_y_ - 0.1f);
    auto_scroll_ = (scroll_y_ >= 0.99f);
    return true;
  }
  
  if (event == Event::ArrowDown || event == Event::PageDown) {
    scroll_y_ = std::min(1.0f, scroll_y_ + 0.1f);
    auto_scroll_ = (scroll_y_ >= 0.99f);
    return true;
  }
  
  if (event == Event::Home) {
    scroll_y_ = 0.0f;
    auto_scroll_ = false;
    return true;
  }
  
  if (event == Event::End) {
    scroll_y_ = 1.0f;
    auto_scroll_ = true;
    return true;
  }
  
  return false;
}