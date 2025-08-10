# Claude Development Guidelines for Native MCP Server CLI

## CRITICAL RULES

### 1. NO CONSOLE OUTPUT DURING RUNTIME
**NEVER use console.log, console.error, console.warn, or any console.* methods AFTER the FTXUI UI starts**
- The application uses FTXUI for terminal UI rendering
- ANY stdout/stderr output during runtime will corrupt the display
- ALL runtime logging MUST go to log files in .logs/ directory
- Console output is OK during startup (before FTXUI starts) for critical errors
- Console output is OK in test files

### 2. Logging Standards
- Use the `log()` function that writes to log files
- Server logs: `.logs/mcp-bridge-server.log`
- Application logs: `.logs/app.log`
- Tool logs: `.logs/tools.log`
- The server MUST use silenceConsole() since it runs alongside FTXUI
- Never output to stdout/stderr during runtime

### 3. Error Handling
- Errors should be logged to files
- Return error objects/codes, don't print them
- Use the event queue for UI notifications

### 4. Testing
- Tests can use console.log since they run separately
- But production code must NEVER use console output

## Project Structure
- C++ TUI frontend with FTXUI
- Node.js MCP bridge server
- All components communicate via TCP/events
- Terminal UI owns the entire screen

## Remember
The terminal is controlled by FTXUI. Any rogue output will break the UI!