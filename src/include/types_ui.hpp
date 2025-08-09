#pragma once

#include "types_core.hpp"
#include <ftxui/screen/color.hpp>

using namespace ftxui;

// ============================================================================
// Color Palette (UI-specific)
// ============================================================================
namespace Colors {
  const auto kBackground = Color::RGB(0x0D, 0x0F, 0x0F);
  const auto kDarkBg = Color::RGB(0x1E, 0x1F, 0x29);
  const auto kGreen = Color::RGB(0x72, 0xF1, 0xB8);
  const auto kBrightGreen = Color::RGB(0x00, 0xFF, 0x9C);
  const auto kDimGreen = Color::RGB(0x66, 0xFF, 0x66);
  const auto kHotPink = Color::RGB(0xFF, 0x2D, 0x95);
  const auto kPink = Color::RGB(0xFF, 0x7E, 0xDB);
  const auto kPurple = Color::RGB(0x9B, 0x5D, 0xF5);
  const auto kCyan = Color::RGB(0x5A, 0xF7, 0x8E);
  const auto kGray = Color::RGB(0x88, 0x88, 0x88);
}