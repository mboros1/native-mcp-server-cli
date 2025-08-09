# Doxygen Documentation Style Guide

## Overview
This guide defines the documentation standards for the Native MCP CLI codebase using Doxygen.

## Documentation Priorities

### High Priority (Document First)
1. **Public APIs** - All public methods and classes
2. **Complex Logic** - Methods with non-obvious behavior
3. **Thread Safety** - Any threading considerations
4. **State Management** - State transitions and side effects
5. **Protocol Integration** - MCP protocol interactions

### Medium Priority
1. **Internal Classes** - Private implementation classes
2. **Helper Functions** - Utility methods
3. **Configuration** - Settings and options

### Low Priority
1. **Simple Getters/Setters** - Self-explanatory accessors
2. **Private Members** - Implementation details
3. **Trivial Destructors** - Default destructors

## Comment Styles

### File Header
```cpp
/**
 * @file filename.hpp
 * @brief One-line description of file purpose
 * @author Author Name
 * @date Year
 * 
 * Detailed description of the file's role in the system.
 * Can span multiple lines.
 */
```

### Class Documentation
```cpp
/**
 * @class ClassName
 * @brief One-line description
 * 
 * Detailed description of the class purpose, responsibilities,
 * and how it fits into the overall architecture.
 * 
 * @note Important usage notes
 * @warning Any warnings about usage
 * 
 * @example Basic Usage
 * @code
 * ClassName obj;
 * obj.doSomething();
 * @endcode
 */
class ClassName {
```

### Method Documentation
```cpp
/**
 * @brief One-line description of what the method does
 * 
 * More detailed description if needed. Explain the "why" and "how",
 * not just the "what".
 * 
 * @param param1 Description of first parameter
 * @param param2 Description of second parameter
 * @return Description of return value
 * 
 * @throws ExceptionType When this exception is thrown
 * 
 * @note Important implementation details
 * @warning Thread safety considerations
 * 
 * @example
 * @code
 * int result = obj.calculate(10, 20);
 * @endcode
 */
int calculate(int param1, int param2);
```

### Enum Documentation
```cpp
/**
 * @enum EnumName
 * @brief One-line description
 * 
 * Detailed description of enum purpose
 */
enum class EnumName {
    VALUE1,  ///< Description of VALUE1
    VALUE2,  ///< Description of VALUE2
    VALUE3   ///< Description of VALUE3
};
```

### Member Variable Documentation
```cpp
/// Brief description of member variable
int member_variable_;

/**
 * @brief More complex member requiring detailed docs
 * 
 * Can include usage notes, constraints, etc.
 */
std::vector<Item> items_;
```

## Special Tags

### Common Doxygen Tags
- `@brief` - Short description (first line)
- `@param` - Parameter description
- `@return` - Return value description
- `@throws` - Exception that may be thrown
- `@note` - Important note for users
- `@warning` - Warning about usage
- `@see` - Cross-reference to related items
- `@todo` - Future improvements
- `@deprecated` - Mark deprecated items
- `@since` - Version when added
- `@example` - Usage example with `@code`

### Grouping
```cpp
/// @name Lifecycle Methods
/// @{
    void initialize();
    void shutdown();
/// @}

/// @name Configuration
/// @{
    void setConfig(const Config& cfg);
    Config getConfig() const;
/// @}
```

## Examples for Native MCP CLI

### StateManager Example
```cpp
/**
 * @brief Set awaiting response state
 * @param message Message being sent to server
 * 
 * Starts timeout tracking and prevents duplicate sends while
 * waiting for server response. The timeout duration is configured
 * via ChatConfig::server_timeout_ms.
 * 
 * @note Blocks new user messages until response received or timeout
 * @warning Must call ClearAwaitingResponse() when done
 * 
 * @example Typical usage in message flow
 * @code
 * state.SetAwaitingResponse("Processing your request...");
 * client.SendMessage(message);
 * // ... wait for response ...
 * state.ClearAwaitingResponse();
 * @endcode
 */
void SetAwaitingResponse(const std::string& message);
```

### Event Handler Example
```cpp
/**
 * @brief Process keyboard/mouse events
 * @param event The event to handle
 * @return true if event was consumed, false to pass to next handler
 * 
 * Processes events in the following priority:
 * 1. Exit shortcuts (Ctrl+C, Ctrl+D)
 * 2. Navigation (arrows, page up/down)
 * 3. Text input
 * 
 * @note Events are processed synchronously
 * @see app::Event for event types
 * 
 * @example Handling custom shortcuts
 * @code
 * if (handler.HandleEvent(Event::CtrlN())) {
 *     // Ctrl+N was handled (new conversation)
 * }
 * @endcode
 */
bool HandleEvent(const app::Event& event);
```

### Thread Safety Example
```cpp
/**
 * @brief Thread-safe message queue
 * 
 * This class provides a lock-free queue for passing messages
 * between the UI thread and network thread.
 * 
 * @note All methods are thread-safe and lock-free
 * @warning Maximum queue size is 1024 messages
 * 
 * @example Producer-consumer pattern
 * @code
 * // Producer thread
 * queue.push(AppEvent{.type = EventType::MESSAGE});
 * 
 * // Consumer thread  
 * AppEvent event;
 * if (queue.try_pop(event)) {
 *     processEvent(event);
 * }
 * @endcode
 */
class MessageQueue {
```

## Best Practices

1. **Focus on "Why" not "What"** - Code shows what, docs explain why
2. **Include Examples** - Show typical usage patterns
3. **Document Assumptions** - State preconditions and postconditions
4. **Cross-Reference** - Use @see to link related items
5. **Be Concise** - Don't over-document obvious things
6. **Keep Updated** - Update docs when changing code
7. **Document Edge Cases** - Explain handling of nulls, empty, errors
8. **Thread Safety** - Always document threading requirements

## Running Doxygen

### Generate Documentation
```bash
doxygen Doxyfile
```

### View Documentation
```bash
open docs/doxygen/html/index.html  # macOS
xdg-open docs/doxygen/html/index.html  # Linux
```

### Check for Warnings
```bash
doxygen Doxyfile 2>&1 | grep -i warning
```

## CI Integration

Documentation is automatically generated and published to GitHub Pages
on every push to the main branch. See `.github/workflows/documentation.yml`.