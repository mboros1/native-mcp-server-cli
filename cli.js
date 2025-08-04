#!/usr/bin/env node

import { fileURLToPath } from 'url';
import { dirname, join } from 'path';
import { existsSync } from 'fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const NATIVE_BINARY = join(__dirname, 'src', 'demo');

if (!existsSync(NATIVE_BINARY)) {
  console.error(`Native binary not found at ${NATIVE_BINARY}`);
  process.exit(1);
}

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
term.onExit(({ exitCode }) => {
  process.stdin.setRawMode(false);
  process.exit(exitCode ?? 0);
});

// Forward signals
process.on('SIGINT', () => term.kill('SIGINT'));
process.on('SIGTERM', () => term.kill('SIGTERM'));

