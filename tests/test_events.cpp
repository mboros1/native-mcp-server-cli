#include <iostream>
#include <cassert>
#include "../src/include/app_events.hpp"

#define TEST(name) std::cout << "Testing: " << name << "... "; 
#define PASS() std::cout << "✓" << std::endl;
#define FAIL(msg) std::cout << "✗ " << msg << std::endl; return 1;
#define ASSERT(cond) if (!(cond)) { FAIL("Assertion failed: " #cond); }

using namespace app;

int main() {
    // Test 1: Event type comparison
    TEST("Event type comparison")
    Event ctrl_c = Event::CtrlC();
    ASSERT(ctrl_c == EventType::CtrlC);
    ASSERT(!(ctrl_c == EventType::CtrlN));
    PASS()
    
    // Test 2: Character events
    TEST("Character events")
    Event char_a = Event::Character("a");
    ASSERT(char_a == EventType::Character);
    ASSERT(char_a.character() == "a");
    
    Event char_emoji = Event::Character("🚀");
    ASSERT(char_emoji.character() == "🚀");
    PASS()
    
    // Test 3: Arrow key events
    TEST("Arrow key events")
    Event up = Event::ArrowUp();
    Event down = Event::ArrowDown();
    ASSERT(up == EventType::ArrowUp);
    ASSERT(down == EventType::ArrowDown);
    ASSERT(up != EventType::ArrowDown);
    ASSERT(down != EventType::ArrowUp);
    PASS()
    
    // Test 4: Mouse events
    TEST("Mouse events")
    MouseInfo info{10, 20, 0};
    Event click(EventType::MouseClick, info);
    ASSERT(click == EventType::MouseClick);
    auto mouse = click.mouse();
    ASSERT(mouse.x == 10);
    ASSERT(mouse.y == 20);
    ASSERT(mouse.button == 0);
    PASS()
    
    // Test 5: Custom events
    TEST("Custom events")
    Event custom = Event::Custom();
    ASSERT(custom == EventType::Custom);
    PASS()
    
    // Test 6: Event type getter
    TEST("Event type getter")
    Event enter = Event::Enter();
    ASSERT(enter.type() == EventType::Enter);
    Event escape = Event::Escape();
    ASSERT(escape.type() == EventType::Escape);
    PASS()
    
    // Test 7: None event (default)
    TEST("None event")
    Event none(EventType::None);
    ASSERT(none == EventType::None);
    ASSERT(none.type() == EventType::None);
    PASS()
    
    // Test 8: Event copying
    TEST("Event copying")
    Event original = Event::Character("test");
    Event copy = original;
    ASSERT(copy == EventType::Character);
    ASSERT(copy.character() == "test");
    PASS()
    
    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}