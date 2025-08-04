#!/usr/bin/env node

import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { existsSync } from 'fs';
import fs from 'fs';
import path from 'path';
import { startServer, stopServer } from './test-server.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const NATIVE_BINARY = join(__dirname, 'src', 'demo');

const logFile = fs.createWriteStream(path.join(__dirname, 'app.log'), { flags: 'a' });

function log(message) {
    const timestamp = new Date().toISOString();
    logFile.write(`[${timestamp}] ${message}\n`);
}

if (!existsSync(NATIVE_BINARY)) {
  console.error(`Native binary not found at ${NATIVE_BINARY}`);
  process.exit(1);
}

// Start the test server and wait for it to be ready
try {
  log('Starting test server...');
  await startServer();
  log('Test server started successfully');
} catch (err) {
  console.error('Failed to start test server:', err);
  process.exit(1);
}

// Now start the TUI
async function startTUI() {
  
  const { default: pty } = await import('node-pty');
  
  const term = pty.spawn(NATIVE_BINARY, [], {
    name: 'xterm-256color',
    cols: process.stdout.columns,
    rows: process.stdout.rows,
    cwd: process.cwd(),
    env: process.env,
  });
  
  // PTY output → real terminal
  term.onData((data) => process.stdout.write(data));
  
  // Real terminal input → PTY
  process.stdin.setRawMode(true);
  process.stdin.resume();
  process.stdin.on('data', (data) => term.write(data));
  
  // Window resize handling
  process.stdout.on('resize', () => {
    term.resize(process.stdout.columns, process.stdout.rows);
  });
  
  // Exit handling
  term.onExit(async ({ exitCode }) => {
    log('TUI exited, shutting down server...');
    process.stdin.setRawMode(false);
    await stopServer();
    process.exit(exitCode ?? 0);
  });
  
  // Forward signals
  process.on('SIGINT', async () => {
    log('SIGINT received');
    term.kill('SIGINT');
    await stopServer();
    process.exit(0);
  });
  process.on('SIGTERM', async () => {
    log('SIGTERM received');
    term.kill('SIGTERM');
    await stopServer();
    process.exit(0);
  });
}

// Start the TUI
startTUI().catch(err => {
  console.error('Failed to start TUI:', err);
  process.exit(1);
});
