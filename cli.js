#!/usr/bin/env node

import { spawn } from 'child_process';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';
import { existsSync, mkdirSync, unlinkSync, rmdirSync, openSync, closeSync } from 'fs';
import { tmpdir } from 'os';
import { program } from 'commander';
import net from 'net';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Path to the C++ executable
const NATIVE_BINARY = join(__dirname, 'src', 'demo');

program
  .name('mcp-cli')
  .description('Native MCP CLI')
  .option('--standalone', 'Run without MCP server (limited functionality)')
  .option('--server', 'Run as MCP server only')
  .option('--verbose', 'Show MCP server output in terminal')
  .parse();

const options = program.opts();

// Check if binary exists (unless running as server)
if (!options.server && !existsSync(NATIVE_BINARY)) {
  console.error('Native binary not found. Please run "npm run build" first.');
  process.exit(1);
}

// Find an available port
async function findAvailablePort() {
  return new Promise((resolve) => {
    const server = net.createServer();
    server.listen(0, () => {
      const port = server.address().port;
      server.close(() => resolve(port));
    });
  });
}

async function main() {
  if (options.server) {
    // Run just the MCP server
    const serverProcess = spawn('node', [join(__dirname, 'mcp-server.js')], {
      stdio: 'inherit'
    });
    
    serverProcess.on('exit', (code) => {
      process.exit(code || 0);
    });
  } else if (options.standalone) {
    // Run CLI without MCP server
    const nativeProcess = spawn(NATIVE_BINARY, [], {
      stdio: 'inherit'  // Give C++ full terminal control
    });
    
    nativeProcess.on('exit', (code) => {
      process.exit(code || 0);
    });
  } else {
    // Run CLI with MCP server integration
    
    // Use a SHORT socket path (macOS sun_path limit is 104 bytes)
    const socketPath = `/tmp/mcp-${process.pid}.sock`;
    
    // Open log files synchronously to get file descriptors
    const logOut = options.verbose ? 'inherit' : openSync('mcp-server-stdout.log', 'a');
    const logErr = options.verbose ? 'inherit' : openSync('mcp-server-stderr.log', 'a');
    
    // Start MCP server on a Unix socket
    const serverProcess = spawn('node', [join(__dirname, 'mcp-server.js'), '--socket', socketPath], {
      stdio: ['ignore', logOut, logErr],  // Use file descriptors or 'inherit'
      env: { ...process.env, MCP_SOCKET: socketPath }
    });
    
    // Close file descriptors on exit if we opened them
    process.on('exit', () => {
      if (typeof logOut === 'number') {
        try { closeSync(logOut); } catch (e) {}
      }
      if (typeof logErr === 'number') {
        try { closeSync(logErr); } catch (e) {}
      }
    });
    
    serverProcess.on('error', (err) => {
      console.error('Failed to start MCP server:', err);
      process.exit(1);
    });
    
    serverProcess.on('exit', (code, signal) => {
      if (code !== 0) {
        console.error('MCP server exited with code:', code, 'signal:', signal);
      }
    });
    
    // Wait for socket file to exist (better than fixed timeout)
    async function waitForSocket(path, timeoutMs = 2000) {
      const end = Date.now() + timeoutMs;
      while (Date.now() < end) {
        if (existsSync(path)) return;
        await new Promise(r => setTimeout(r, 50));
      }
      throw new Error('MCP server never created socket');
    }
    
    try {
      await waitForSocket(socketPath);
    } catch (err) {
      console.error(err.message);
      serverProcess.kill();
      process.exit(1);
    }
    
    // Then spawn the native CLI with full terminal control
    const nativeProcess = spawn(NATIVE_BINARY, [], {
      stdio: 'inherit',  // C++ gets full terminal control
      env: { ...process.env, MCP_SOCKET: socketPath }  // Pass socket via env only
    });
    
    // Handle cleanup
    const cleanup = () => {
      serverProcess.kill('SIGTERM');
      try {
        unlinkSync(socketPath);
      } catch (e) {}
    };
    
    // Forward signals properly
    process.on('SIGINT', () => {
      nativeProcess.kill('SIGINT');
    });
    
    process.on('SIGTERM', () => {
      nativeProcess.kill('SIGTERM');
    });
    
    nativeProcess.on('exit', (code) => {
      cleanup();
      process.exit(code || 0);
    });
  }
}

main().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});