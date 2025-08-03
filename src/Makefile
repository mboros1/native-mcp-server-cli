# ---- toolchain -------------------------------------------------------------
CXX      ?= g++
CXXFLAGS += -std=c++20 -Wall -Wextra -pedantic

# Include paths
INCLUDES = -I../deps/simdjson

# Try pkg-config first, fall back to manual paths if needed
PKG_CONFIG := pkg-config
FTXUI_CFLAGS := $(shell $(PKG_CONFIG) --cflags ftxui 2>/dev/null)
FTXUI_LIBS := $(shell $(PKG_CONFIG) --libs ftxui 2>/dev/null)

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

CXXFLAGS += $(FTXUI_CFLAGS) $(INCLUDES)
LDFLAGS  += $(FTXUI_LIBS)

# ---- build targets ---------------------------------------------------------
demo: main.cpp ../deps/simdjson/simdjson.cpp
	$(CXX) $(CXXFLAGS) main.cpp ../deps/simdjson/simdjson.cpp -o $@ $(LDFLAGS)

all: demo
clean:
	rm -f demo
.PHONY: all clean