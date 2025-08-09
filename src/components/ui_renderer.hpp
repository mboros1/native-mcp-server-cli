#ifndef UI_RENDERER_HPP
#define UI_RENDERER_HPP

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <string>
#include <vector>
#include <sstream>
#include "../include/types_ui.hpp"
#include "../core/state_manager.hpp"

using namespace ftxui;

class UIRenderer {
private:
  const Config& config_;
  const StateManager& state_;
  const std::vector<Tool>& tools_;
  bool is_connected_ = false;

  Elements RenderHelp() const;
  Elements RenderToolList() const;
  Elements RenderToolInfo(const Tool& tool) const;

public:
  UIRenderer(const Config& config, const StateManager& state, const std::vector<Tool>& tools);
    
  void SetConnectionStatus(bool connected);
  std::string GetScreenText() const;
  Element RenderConversationLog() const;
  Element Render() const;
};

#endif // UI_RENDERER_HPP