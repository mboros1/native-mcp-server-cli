/**
 * Test MCP tools functionality
 */

import { TestRunner, assert, sleep } from './test-utils.js';
import MockTcpClient from './mock-tcp-client.js';
import { startServer, stopServer } from '../mcp-bridge-server.js';
import { listFiles } from '../tools/listFiles.js';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const runner = new TestRunner('MCP Tools Tests');

// Start server for integration tests
let server;
try {
  server = await startServer(4003);
  console.log('Test server started on port 4003');
} catch (err) {
  console.error('Failed to start server:', err);
  process.exit(1);
}

// Test 1: Direct list_files function test
await runner.testAsync('Direct list_files execution', async () => {
  // Set workspace root to project directory
  process.env.WORKSPACE_ROOT = path.resolve(__dirname, '../..');
  
  const result = await listFiles({
    base_path: 'server',
    pattern: '*.js',
    recursive: false,
    include_dirs: false,
    limit: 10
  });
  
  assert.ok(result.entries, 'Should return entries array');
  assert.ok(Array.isArray(result.entries), 'Entries should be an array');
  assert.ok(result.entries.length > 0, 'Should find some JS files in server directory');
  
  // Check entry structure
  const firstEntry = result.entries[0];
  assert.ok(firstEntry.path, 'Entry should have path');
  assert.equal(firstEntry.type, 'file', 'Entry should be a file');
  assert.ok(typeof firstEntry.size === 'number', 'Entry should have numeric size');
  assert.ok(firstEntry.mtime, 'Entry should have mtime');
});

// Test 2: List files with pattern matching
await runner.testAsync('Pattern matching with glob', async () => {
  const result = await listFiles({
    base_path: 'server/test',
    pattern: 'test-*.js',
    recursive: false
  });
  
  assert.ok(result.entries.length > 0, 'Should find test files');
  
  // All results should match pattern
  for (const entry of result.entries) {
    const filename = path.basename(entry.path);
    assert.ok(filename.startsWith('test-'), `File ${filename} should match pattern`);
  }
});

// Test 3: Include directories
await runner.testAsync('Include directories in results', async () => {
  const result = await listFiles({
    base_path: '.',
    pattern: '*',
    include_dirs: true,
    recursive: false,
    limit: 50
  });
  
  const dirs = result.entries.filter(e => e.type === 'dir');
  const files = result.entries.filter(e => e.type === 'file');
  
  assert.ok(dirs.length > 0, 'Should find some directories');
  assert.ok(files.length > 0, 'Should find some files');
});

// Test 4: Path traversal protection
await runner.testAsync('Path traversal protection', async () => {
  try {
    await listFiles({
      base_path: '../../../etc',
      pattern: '*'
    });
    assert.ok(false, 'Should have thrown error for path traversal');
  } catch (err) {
    assert.includes(err.message, 'outside workspace', 'Should reject path outside workspace');
  }
});

// Test 5: Sorting functionality
await runner.testAsync('Sort by name ascending', async () => {
  const result = await listFiles({
    base_path: 'server',
    pattern: '*.js',
    sort_by: 'name',
    order: 'asc'
  });
  
  if (result.entries.length > 1) {
    for (let i = 1; i < result.entries.length; i++) {
      const prev = result.entries[i - 1].path.toLowerCase();
      const curr = result.entries[i].path.toLowerCase();
      assert.ok(prev <= curr, `Files should be sorted: ${prev} <= ${curr}`);
    }
  }
});

// Test 6: Limit enforcement
await runner.testAsync('Limit enforcement', async () => {
  const result = await listFiles({
    base_path: '.',
    pattern: '**/*',
    recursive: true,
    limit: 5
  });
  
  assert.ok(result.entries.length <= 5, 'Should respect limit');
  if (result.truncated) {
    assert.equal(result.entries.length, 5, 'Should return exactly limit when truncated');
  }
});

// Test 7: Server tool list request
await runner.testAsync('Server returns tool list', async () => {
  const client = new MockTcpClient(4003);
  await client.connect();
  
  client.send({ type: 'tool_list' });
  const response = await client.waitForMessage('tool_list_response');
  
  assert.equal(response.type, 'tool_list_response');
  assert.ok(Array.isArray(response.tools), 'Should return tools array');
  assert.ok(response.tools.length > 0, 'Should have at least one tool');
  
  // Check list_files tool exists
  const listFilesTool = response.tools.find(t => t.name === 'list_files');
  assert.ok(listFilesTool, 'Should include list_files tool');
  assert.ok(listFilesTool.description, 'Tool should have description');
  assert.ok(listFilesTool.parameters, 'Tool should have parameters');
  
  await client.disconnect();
});

// Test 8: Server tool execution
await runner.testAsync('Server executes list_files tool', async () => {
  const client = new MockTcpClient(4003);
  await client.connect();
  
  client.send({
    type: 'tool_execute',
    tool_name: 'list_files',
    arguments: {
      base_path: 'server',
      pattern: '*.js',
      limit: 3
    }
  });
  
  const response = await client.waitForMessage('tool_result');
  
  assert.equal(response.type, 'tool_result');
  assert.equal(response.tool_name, 'list_files');
  assert.ok(response.success, 'Tool should execute successfully');
  assert.ok(response.result, 'Should have result');
  assert.ok(Array.isArray(response.result.entries), 'Result should have entries array');
  
  await client.disconnect();
});

// Test 9: Server handles tool execution error
await runner.testAsync('Server handles tool errors gracefully', async () => {
  const client = new MockTcpClient(4003);
  await client.connect();
  
  client.send({
    type: 'tool_execute',
    tool_name: 'list_files',
    arguments: {
      base_path: '/etc/passwd/../../../'  // Invalid path
    }
  });
  
  const response = await client.waitForMessage('tool_result');
  
  assert.equal(response.type, 'tool_result');
  assert.equal(response.tool_name, 'list_files');
  assert.ok(!response.success, 'Tool should fail');
  assert.ok(response.error, 'Should have error message');
  
  await client.disconnect();
});

// Test 10: Unknown tool handling
await runner.testAsync('Server rejects unknown tools', async () => {
  const client = new MockTcpClient(4003);
  await client.connect();
  
  client.send({
    type: 'tool_execute',
    tool_name: 'nonexistent_tool',
    arguments: {}
  });
  
  const response = await client.waitForMessage('tool_result');
  
  assert.equal(response.type, 'tool_result');
  assert.ok(!response.success, 'Should fail for unknown tool');
  assert.includes(response.error, 'Unknown tool', 'Error should mention unknown tool');
  
  await client.disconnect();
});

// Cleanup
await stopServer();
console.log('Test server stopped');

process.exit(runner.summary());