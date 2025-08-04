import { EventEmitter } from 'events';
import { createInterface } from 'readline';

/**
 * MCPBridge handles communication between the C++ TUI and MCP servers
 */
export class MCPBridge extends EventEmitter {
  constructor(nativeProcess) {
    super();
    this.nativeProcess = nativeProcess;
    this.servers = new Map();
    this.rl = null;
    this.pendingResponses = new Map();
  }

  async start() {
    // Set up readline interface for IPC messages on stderr
    this.rl = createInterface({
      input: this.nativeProcess.stderr,
      terminal: false
    });

    // Listen for IPC commands from the C++ process via stderr
    this.rl.on('line', (line) => {
      try {
        const message = JSON.parse(line);
        this.handleNativeMessage(message);
      } catch (err) {
        // Only log if it's actually JSON-like (not regular log output)
        if (line.trim().startsWith('{')) {
          console.error('Failed to parse IPC message:', err);
        }
      }
    });

    console.log('MCP Bridge started. Chat messages will be processed by AI services.');
    console.log('Type "/help" for commands or just type to chat.\n');
  }

  async handleNativeMessage(message) {
    switch (message.type) {
      case 'chat':
        await this.handleChatMessage(message);
        break;
      
      case 'tool_call':
        await this.handleToolCall(message);
        break;
      
      case 'connect_server':
        await this.connectToServer(message);
        break;
      
      default:
        console.error('Unknown message type:', message.type);
    }
  }

  async handleChatMessage(message) {
    console.log(`\n💬 Processing: "${message.content}"`);
    
    // TODO: Implement actual API calls to OpenAI, Anthropic, etc.
    // For now, just echo back with a simulated response
    setTimeout(() => {
      console.log('🤖 Assistant: I received your message! Chat integration coming soon.');
      console.log('   (This will connect to OpenAI/Anthropic/other AI services)\n');
    }, 500);
  }

  async handleToolCall(message) {
    const { tool, parameters } = message;
    console.log(`\n🔧 Tool call: ${tool}`);
    console.log('   Parameters:', parameters);
    
    // TODO: Forward to connected MCP servers
    this.sendToNative({
      type: 'tool_response',
      id: message.id,
      result: {
        status: 'pending',
        message: 'Tool execution will be implemented with MCP server connections'
      }
    });
  }

  async connectToServer(message) {
    console.log(`\n🔌 Connecting to MCP server: ${message.server}`);
    // TODO: Implement WebSocket or stdio connection to MCP servers
  }

  sendToNative(message) {
    // Send responses back via stdin
    this.nativeProcess.stdin.write(JSON.stringify(message) + '\n');
  }
}