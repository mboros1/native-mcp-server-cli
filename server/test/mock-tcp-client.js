/**
 * Mock TCP client for testing the MCP bridge server
 */

import net from 'net';
import { EventEmitter } from 'events';

class MockTcpClient extends EventEmitter {
  constructor(port = 4000, host = '127.0.0.1') {
    super();
    this.port = port;
    this.host = host;
    this.socket = null;
    this.connected = false;
    this.messages = [];
    this.buffer = '';
  }

  /**
   * Connect to the server
   */
  connect() {
    return new Promise((resolve, reject) => {
      this.socket = new net.Socket();
      
      this.socket.connect(this.port, this.host, () => {
        this.connected = true;
        this.emit('connected');
        resolve();
      });

      this.socket.on('data', (data) => {
        this.handleData(data.toString());
      });

      this.socket.on('error', (err) => {
        this.emit('error', err);
        reject(err);
      });

      this.socket.on('close', () => {
        this.connected = false;
        this.emit('disconnected');
      });

      // Set timeout for connection
      setTimeout(() => {
        if (!this.connected) {
          this.socket.destroy();
          reject(new Error('Connection timeout'));
        }
      }, 5000);
    });
  }

  /**
   * Handle incoming data (may be multiple messages)
   */
  handleData(data) {
    this.buffer += data;
    
    // Split by newlines to handle multiple messages
    const lines = this.buffer.split('\n');
    
    // Keep the last incomplete line in the buffer
    this.buffer = lines.pop() || '';
    
    // Process complete lines
    for (const line of lines) {
      if (line.trim()) {
        try {
          const message = JSON.parse(line);
          this.messages.push(message);
          this.emit('message', message);
          
          // Emit specific event types
          if (message.type) {
            this.emit(message.type, message);
          }
        } catch (err) {
          console.error('Failed to parse message:', line);
          this.emit('parse_error', err);
        }
      }
    }
  }

  /**
   * Send a message to the server
   */
  send(message) {
    if (!this.connected) {
      throw new Error('Not connected');
    }
    
    const data = JSON.stringify(message) + '\n';
    this.socket.write(data);
  }

  /**
   * Send a chat message
   */
  sendChat(content, options = {}) {
    this.send({
      type: 'chat',
      content,
      ...options
    });
  }

  /**
   * Send a retry request
   */
  sendRetry(originalMessage = null) {
    this.send({
      type: 'retry',
      originalMessage
    });
  }

  /**
   * Send a reset/interrupt request
   */
  sendReset() {
    this.send({
      type: 'reset'
    });
  }

  /**
   * Send a reload request
   */
  sendReload() {
    this.send({
      type: 'reload',
      content: 'chat_history'
    });
  }

  /**
   * Send a sync request
   */
  sendSync() {
    this.send({
      type: 'sync',
      content: 'request_history'
    });
  }

  /**
   * Wait for a specific message type
   */
  waitForMessage(type, timeout = 5000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.removeListener(type, handler);
        reject(new Error(`Timeout waiting for ${type} message`));
      }, timeout);

      const handler = (message) => {
        clearTimeout(timer);
        resolve(message);
      };

      this.once(type, handler);
    });
  }

  /**
   * Wait for a response message
   */
  async waitForResponse(timeout = 10000) {
    return this.waitForMessage('response', timeout);
  }

  /**
   * Get the last message received
   */
  getLastMessage() {
    return this.messages[this.messages.length - 1];
  }

  /**
   * Clear received messages
   */
  clearMessages() {
    this.messages = [];
  }

  /**
   * Disconnect from the server
   */
  disconnect() {
    return new Promise((resolve) => {
      if (!this.connected) {
        resolve();
        return;
      }

      this.socket.once('close', () => {
        resolve();
      });

      this.socket.end();
    });
  }

  /**
   * Force close the connection
   */
  destroy() {
    if (this.socket) {
      this.socket.destroy();
    }
    this.connected = false;
  }
}

export default MockTcpClient;