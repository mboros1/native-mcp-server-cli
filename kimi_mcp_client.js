import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { StdioClientTransport } from "@modelcontextprotocol/sdk/client/stdio.js";

// Launch server as a subprocess, connect transport
import { spawn } from "child_process";

// Parse CLI argument as message
const [, , ...args] = process.argv;
const userMessage = args.join(' ') || 'Hello from MCP!';

// Start server as subprocess
const serverProcess = spawn('node', ['kimi_mcp_server.js'], {
  stdio: ['pipe', 'pipe', 'inherit']
});

// MCP client using stdio transport
const transport = new StdioClientTransport({
  stdin: serverProcess.stdout,
  stdout: serverProcess.stdin,
  // (not reversed; that's how the SDK expects piping)
});

const client = new Client({
  name: "kimi-mcp-client",
  version: "1.0.0"
});

(async () => {
  await client.connect(transport);

  // Call the "chat" tool
  const result = await client.callTool({
    name: "chat",
    arguments: { message: userMessage }
  });

  // Result format is:
  // { content: [{ type: "text", text: ... }] }
  if (result.content && result.content.length > 0) {
    console.log(result.content.map(x => x.text).join("\n"));
  } else {
    console.log(result);
  }
  serverProcess.kill();
})();

