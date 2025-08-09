#pragma once

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/event.hpp>
#include <algorithm>

using namespace ftxui;

class ConversationLogManager {
private:
  ScreenInteractive* screen_ = nullptr;

public:
  float scroll_y_ = 1.0f;  // Start at bottom (1.0 = 100% scrolled down)
  bool auto_scroll_ = true; // Auto-scroll to bottom on new messages
  
  void SetScreen(ScreenInteractive* screen);
  void OnNewMessage();
  bool HandleScrollEvent(Event event);
};