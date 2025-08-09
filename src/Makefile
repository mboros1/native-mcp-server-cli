# ---- toolchain -------------------------------------------------------------
CXX      ?= g++
CXXFLAGS += -std=c++20 -Wall -Wextra -pedantic

# Output directory for binaries
BIN_DIR = bin

# Include paths
INCLUDES = -I../deps/simdjson -I../deps -I../deps/asio -I../deps/json_struct -I../deps/atomic_queue -I../deps/token-estimator -I. -Iinclude

# Source files for main application
SOURCES = main.cpp \
          network/mcp_client.cpp \
          core/state_manager.cpp \
          core/input_handler.cpp \
          components/conversation_log_manager.cpp \
          components/input_with_history.cpp \
          components/ui_renderer.cpp \
          ../deps/simdjson/simdjson.cpp \
          ../deps/token-estimator/token_est.cpp

# Source files for headless application (no UI components)
HEADLESS_SOURCES = core/headless_application.cpp \
                   network/mcp_client.cpp \
                   core/state_manager.cpp \
                   core/input_handler.cpp \
                   ../deps/simdjson/simdjson.cpp \
                   ../deps/token-estimator/token_est.cpp

# Test sources
TEST_SOURCES = ../tests/example_headless_tests.cpp
SIMPLE_TEST_SOURCES = ../tests/simple_headless_test.cpp
PERSIST_TEST_SOURCES = ../tests/test_chat_persistence.cpp

# Try pkg-config first, fall back to manual paths if needed
PKG_CONFIG := pkg-config
FTXUI_CFLAGS := $(shell $(PKG_CONFIG) --cflags ftxui 2>/dev/null)
FTXUI_LIBS := $(shell $(PKG_CONFIG) --libs ftxui 2>/dev/null)
SPDLOG_CFLAGS := $(shell $(PKG_CONFIG) --cflags spdlog 2>/dev/null)
SPDLOG_LIBS := $(shell $(PKG_CONFIG) --libs spdlog 2>/dev/null)

# If pkg-config fails, try common paths
ifeq ($(FTXUI_LIBS),)
  # macOS with Homebrew
  ifeq ($(shell uname),Darwin)
    ifneq ($(wildcard /opt/homebrew/lib/libftxui-component.a),)
      # Apple Silicon
      FTXUI_CFLAGS := -I/opt/homebrew/include
      FTXUI_LIBS := -L/opt/homebrew/lib
    else ifneq ($(wildcard /usr/local/lib/libftxui-component.a),)
      # Intel Mac
      FTXUI_CFLAGS := -I/usr/local/include
      FTXUI_LIBS := -L/usr/local/lib
    endif
  else
    # Linux default paths
    FTXUI_CFLAGS := -I/usr/include
    FTXUI_LIBS := -L/usr/lib
  endif
  FTXUI_LIBS += -lftxui-component -lftxui-dom -lftxui-screen
endif

# Full UI build flags
CXXFLAGS_UI = $(CXXFLAGS) $(FTXUI_CFLAGS) $(SPDLOG_CFLAGS) $(INCLUDES)
LDFLAGS_UI  = $(FTXUI_LIBS) $(SPDLOG_LIBS)

# Headless build flags (no FTXUI)
CXXFLAGS_HEADLESS = $(CXXFLAGS) $(SPDLOG_CFLAGS) $(INCLUDES)
LDFLAGS_HEADLESS  = $(SPDLOG_LIBS)

# ---- build targets ---------------------------------------------------------

# Default target - build the full UI application
$(BIN_DIR)/demo: $(SOURCES) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_UI) $(SOURCES) -o $@ $(LDFLAGS_UI)

# Headless library for testing (static library)
libheadless.a: $(HEADLESS_SOURCES)
	$(CXX) $(CXXFLAGS_HEADLESS) -c $(HEADLESS_SOURCES)
	ar rcs $@ *.o
	rm -f *.o

# Headless test executable
$(BIN_DIR)/test-headless: libheadless.a $(TEST_SOURCES) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HEADLESS) $(TEST_SOURCES) -L. -lheadless -o $@ $(LDFLAGS_HEADLESS)

# Simple headless test executable
$(BIN_DIR)/simple-test: libheadless.a $(SIMPLE_TEST_SOURCES) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HEADLESS) $(SIMPLE_TEST_SOURCES) -L. -lheadless -o $@ $(LDFLAGS_HEADLESS)

# Run headless tests
test: $(BIN_DIR)/test-headless
	$(BIN_DIR)/test-headless

# Run simple test
test-simple: $(BIN_DIR)/simple-test
	$(BIN_DIR)/simple-test

# Individual unit tests
$(BIN_DIR)/test-persist: libheadless.a $(PERSIST_TEST_SOURCES) | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HEADLESS) $(PERSIST_TEST_SOURCES) -L. -lheadless -o $@ $(LDFLAGS_HEADLESS)

$(BIN_DIR)/test-state: libheadless.a ../tests/test_state_manager.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HEADLESS) ../tests/test_state_manager.cpp -L. -lheadless -o $@ $(LDFLAGS_HEADLESS)

$(BIN_DIR)/test-events: ../tests/test_events.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HEADLESS) ../tests/test_events.cpp -o $@ $(LDFLAGS_HEADLESS)

$(BIN_DIR)/test-input: libheadless.a ../tests/test_input_handler.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HEADLESS) ../tests/test_input_handler.cpp -L. -lheadless -o $@ $(LDFLAGS_HEADLESS)

$(BIN_DIR)/test-input-server: libheadless.a ../tests/test_input_handler_with_server.cpp | $(BIN_DIR)
	$(CXX) $(CXXFLAGS_HEADLESS) ../tests/test_input_handler_with_server.cpp -L. -lheadless -o $@ $(LDFLAGS_HEADLESS)

# Run all unit tests
test-all: $(BIN_DIR)/test-persist $(BIN_DIR)/test-state $(BIN_DIR)/test-events $(BIN_DIR)/test-input
	@./run_tests.sh

# Build everything
all: $(BIN_DIR)/demo $(BIN_DIR)/test-headless $(BIN_DIR)/simple-test

# Create bin directory
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Clean build artifacts
clean:
	rm -rf $(BIN_DIR) libheadless.a *.o

# Convenience targets (without bin/ prefix)
demo: $(BIN_DIR)/demo
test-headless: $(BIN_DIR)/test-headless
simple-test: $(BIN_DIR)/simple-test
test-persist: $(BIN_DIR)/test-persist
test-state: $(BIN_DIR)/test-state
test-events: $(BIN_DIR)/test-events
test-input: $(BIN_DIR)/test-input

# Phony targets
.PHONY: all clean test test-simple test-all demo test-headless simple-test test-persist test-state test-events test-input