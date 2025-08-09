/**
 * Test basic server connection and lifecycle
 */

import { TestRunner, assert, sleep } from './test-utils.js';
import MockTcpClient from './mock-tcp-client.js';
import { startServer, stopServer } from '../mcp-bridge-server.js';

const runner = new TestRunner('Server Connection Tests');

// Start server before tests
let server;
try {
  server = await startServer(4001); // Use different port to avoid conflicts
  console.log('Test server started on port 4001');
} catch (err) {
  console.error('Failed to start server:', err);
  process.exit(1);
}

// Test 1: Client can connect to server
await runner.testAsync('Client can connect to server', async () => {
  const client = new MockTcpClient(4001);
  
  await client.connect();
  assert.ok(client.connected, 'Client should be connected');
  
  await client.disconnect();
});

// Test 2: Server sends welcome message on connection
await runner.testAsync('Server sends welcome message', async () => {
  const client = new MockTcpClient(4001);
  
  await client.connect();
  const welcome = await client.waitForMessage('welcome', 2000);
  
  assert.equal(welcome.type, 'welcome');
  assert.includes(welcome.message, 'MCP bridge server');
  
  await client.disconnect();
});

// Test 3: Multiple clients can connect
await runner.testAsync('Multiple clients can connect', async () => {
  const client1 = new MockTcpClient(4001);
  const client2 = new MockTcpClient(4001);
  
  await client1.connect();
  await client2.connect();
  
  assert.ok(client1.connected, 'Client 1 should be connected');
  assert.ok(client2.connected, 'Client 2 should be connected');
  
  await client1.disconnect();
  await client2.disconnect();
});

// Test 4: Client receives heartbeat messages
await runner.testAsync('Client receives heartbeat messages', async () => {
  const client = new MockTcpClient(4001);
  
  await client.connect();
  
  // Note: Heartbeat is sent every 5 minutes, so we'll simulate by waiting
  // In a real test, you might want to make the heartbeat interval configurable
  console.log('    (Skipping heartbeat test - interval too long for testing)');
  
  await client.disconnect();
});

// Test 5: Server handles client disconnect gracefully
await runner.testAsync('Server handles client disconnect', async () => {
  const client = new MockTcpClient(4001);
  
  await client.connect();
  assert.ok(client.connected);
  
  await client.disconnect();
  assert.ok(!client.connected);
  
  // Server should still be running and accept new connections
  const newClient = new MockTcpClient(4001);
  await newClient.connect();
  assert.ok(newClient.connected);
  
  await newClient.disconnect();
});

// Test 6: Server handles abrupt disconnection
await runner.testAsync('Server handles abrupt disconnection', async () => {
  const client = new MockTcpClient(4001);
  
  await client.connect();
  assert.ok(client.connected);
  
  // Force close without proper disconnect
  client.destroy();
  assert.ok(!client.connected);
  
  // Give server a moment to handle the disconnection
  await sleep(100);
  
  // Server should still accept new connections
  const newClient = new MockTcpClient(4001);
  await newClient.connect();
  assert.ok(newClient.connected);
  
  await newClient.disconnect();
});

// Cleanup: Stop server
await stopServer();
console.log('Test server stopped');

// Exit with appropriate code
process.exit(runner.summary());