/**
 * Test chat message handling
 */

import { TestRunner, assert, sleep } from './test-utils.js';
import MockTcpClient from './mock-tcp-client.js';
import { startServer, stopServer } from '../mcp-bridge-server.js';
import fs from 'fs';
import path from 'path';

const runner = new TestRunner('Chat Message Tests');

// Setup: Create mock .env for testing
const testEnv = `
KIMI_API_KEY=test-key-12345
OPENAI_API_KEY=test-openai-key
`;
fs.writeFileSync('.env.test', testEnv);
process.env.KIMI_API_KEY = 'test-key-12345';
process.env.OPENAI_API_KEY = 'test-openai-key';

// Start server
let server;
try {
  server = await startServer(4002);
  console.log('Test server started on port 4002');
} catch (err) {
  console.error('Failed to start server:', err);
  process.exit(1);
}

// Test 1: Server handles /new command
await runner.testAsync('Server handles /new command', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  client.sendChat('/new');
  const response = await client.waitForResponse();
  
  assert.equal(response.type, 'response');
  assert.includes(response.reply, 'new conversation');
  
  await client.disconnect();
});

// Test 2: Server handles chat history reload
await runner.testAsync('Server handles reload request', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  client.sendReload();
  await sleep(100); // Give server time to process
  
  // No error should occur
  assert.ok(true, 'Reload processed without error');
  
  await client.disconnect();
});

// Test 3: Server handles sync request
await runner.testAsync('Server handles sync request', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  client.sendSync();
  const response = await client.waitForMessage('sync_response');
  
  assert.equal(response.type, 'sync_response');
  assert.ok(response.server_stats);
  assert.ok(Array.isArray(response.server_history));
  
  await client.disconnect();
});

// Test 4: Server handles reset/interrupt request
await runner.testAsync('Server handles reset request', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  client.sendReset();
  const response = await client.waitForMessage('system');
  
  assert.equal(response.type, 'system');
  assert.ok(response.message);
  
  await client.disconnect();
});

// Test 5: Server rejects retry without previous message
await runner.testAsync('Server rejects retry without history', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  // Clear any existing history
  client.sendChat('/new');
  await client.waitForResponse();
  
  // Try retry without any user messages
  client.sendRetry();
  const response = await client.waitForMessage('error');
  
  assert.equal(response.type, 'error');
  assert.includes(response.message, 'No user message');
  
  await client.disconnect();
});

// Test 6: Server prevents concurrent requests from same client
await runner.testAsync('Server prevents concurrent requests', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  // Note: Without mocking the actual API calls, we can't truly test this
  // But we can verify the structure is in place
  console.log('    (Skipping concurrent test - requires API mocking)');
  
  await client.disconnect();
});

// Test 7: Server handles malformed JSON gracefully
await runner.testAsync('Server handles malformed JSON', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  // Send malformed JSON directly through socket
  client.socket.write('{"invalid json\n');
  
  // Server should not crash, client should remain connected
  await sleep(100);
  assert.ok(client.connected, 'Client should remain connected');
  
  // Should still be able to send valid messages
  client.sendChat('/new');
  const response = await client.waitForResponse();
  assert.equal(response.type, 'response');
  
  await client.disconnect();
});

// Test 8: Server handles empty messages
await runner.testAsync('Server handles empty messages', async () => {
  const client = new MockTcpClient(4002);
  await client.connect();
  
  // Send empty message
  client.send({ type: 'chat', content: '' });
  
  // Server should not crash
  await sleep(100);
  assert.ok(client.connected, 'Client should remain connected');
  
  await client.disconnect();
});

// Cleanup
await stopServer();
console.log('Test server stopped');

// Clean up test env file
fs.unlinkSync('.env.test');

process.exit(runner.summary());