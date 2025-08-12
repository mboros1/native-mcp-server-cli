FROM node:20

# Install build tools for C++
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libfmt-dev \
    libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# Copy package files
COPY package*.json ./
COPY server/package*.json ./server/

# Install dependencies
RUN npm install
RUN cd server && npm install

# Copy source files
COPY . .

# Run tests
CMD ["bash", "-c", "./scripts/run_js_tests.sh"]