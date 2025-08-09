#pragma once

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <deque>
#include <string>
#include <fstream>
#include <filesystem>
#include <spdlog/spdlog.h>

using namespace ftxui;

class InputWithHistory : public ComponentBase {
private:
  std::deque<std::string> command_history_;
  std::string current_input_;
  int history_index_ = -1;  // -1 means not browsing history
  std::string* input_content_;
  Component input_component_;
  InputOption input_options_;
  
public:
  InputWithHistory(std::string* content, InputOption options);
  
  void AddToHistory(const std::string& command);
  bool OnEvent(Event event) override;
  
  // Expose the underlying input component for external access
  Component GetInputComponent();
  
private:
  void NavigateHistoryUp();
  void NavigateHistoryDown();
  void LoadHistory();
  void SaveHistory();
  std::string EscapeString(const std::string& str);
  std::string UnescapeString(const std::string& str);
};