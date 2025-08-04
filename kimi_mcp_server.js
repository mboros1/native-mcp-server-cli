import 'dotenv/config';
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import Anthropic from "@anthropic-ai/sdk";

// Set up Anthropic/Kimi K2
const anthropic = new Anthropic({
  apiKey: process.env.KIMI_API_KEY,
  baseURL: 'https://kimi-k2.ai/api/v1',
});

const server = new McpServer({
  name: "kimi-k2-server",
  version: "1.0.0"
});

// Register a Tool (the standard way to expose actions in MCP)
server.registerTool(
  "chat",
  {
    title: "Kimi Chat",
    description: "Send a message to the Kimi-K2 model",
    inputSchema: {
      message: z.string().describe("User message to send to the model")
    }
  },
  async ({ message }) => {
    const res = await anthropic.messages.create({
      model: "kimi-k2",
      messages: [{ role: 'user', content: message }],
      max_tokens: 1024
    });
    let reply = res.content;
    if (Array.isArray(reply)) reply = reply.map(c => c.text || c).join('');
    return {
      content: [{ type: "text", text: reply }]
    };
  }
);

// Start server using stdio transport (for CLI, MCP tools, or piping)
const transport = new StdioServerTransport();
await server.connect(transport);

console.log("MCP Kimi-K2 server running!");

