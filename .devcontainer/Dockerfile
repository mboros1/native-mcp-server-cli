FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-22.04

# Install system dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libspdlog-dev \
    libfmt-dev \
    bear \
    gdb \
    lldb \
    git \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Install FTXUI from source
WORKDIR /tmp
RUN git clone https://github.com/ArthurSonzogni/FTXUI.git && \
    cd FTXUI && \
    mkdir build && cd build && \
    cmake .. -GNinja \
        -DCMAKE_BUILD_TYPE=Release \
        -DFTXUI_BUILD_EXAMPLES=OFF \
        -DFTXUI_BUILD_TESTS=OFF \
        -DFTXUI_BUILD_DOCS=OFF && \
    ninja && \
    ninja install && \
    ldconfig && \
    cd / && rm -rf /tmp/FTXUI

# Install Node.js 20
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - && \
    apt-get install -y nodejs && \
    rm -rf /var/lib/apt/lists/*

# Set up non-root user
ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

# Create workspace directory
RUN mkdir -p /workspace && chown -R $USERNAME:$USERNAME /workspace

WORKDIR /workspace

# Switch to non-root user
USER $USERNAME

# Set environment variables for the terminal UI
ENV TERM=xterm-256color
ENV COLORTERM=truecolor