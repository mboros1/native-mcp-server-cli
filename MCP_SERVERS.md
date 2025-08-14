# MCP Servers Configuration

Your MCP bridge server now supports dynamic loading of multiple MCP servers through the `mcp-servers.json` configuration file.

## Currently Configured Servers

### 1. Filesystem Server ✅ (Enabled)
- **Package**: `@modelcontextprotocol/server-filesystem`
- **Status**: Active
- **Tools Available**: 14 tools for file operations
  - `filesystem_read_file` - Read file contents
  - `filesystem_write_file` - Write to files
  - `filesystem_edit_file` - Make line-based edits
  - `filesystem_create_directory` - Create directories
  - `filesystem_list_directory` - List directory contents
  - `filesystem_move_file` - Move/rename files
  - `filesystem_search_files` - Search for files
  - `filesystem_get_file_info` - Get file metadata
  - And more...

### 2. GitHub Server ✅ (Enabled)
- **Package**: `@modelcontextprotocol/server-github`
- **Status**: Active (requires GITHUB_TOKEN environment variable)
- **Tools Available**: 26 tools for GitHub operations
  - `github_create_repository` - Create new repos
  - `github_create_issue` - Create issues
  - `github_create_pull_request` - Create PRs
  - `github_search_repositories` - Search repos
  - `github_search_code` - Search code
  - `github_get_file_contents` - Read files from repos
  - `github_push_files` - Push changes
  - `github_list_commits` - List commit history
  - `github_merge_pull_request` - Merge PRs
  - And many more...

### 3. Serper Search & Scrape ⚠️ (Disabled - needs API key)
- **Package**: `@marcopesani/mcp-server-serper`
- **Status**: Disabled (requires SERPER_API_KEY)
- **Description**: Web search and webpage scraping via Serper API
- **Tools When Enabled**:
  - `serper_google_search` - Perform Google searches
  - Web scraping with markdown/text extraction
  - Advanced search operators support
  
To enable: 
1. Get API key from https://serper.dev
2. Set environment variable: `export SERPER_API_KEY=your_key`
3. Change `"enabled": true` in mcp-servers.json

### 4. SQLite Server ⚠️ (Disabled)
- **Package**: `@modelcontextprotocol/server-sqlite`
- **Status**: Disabled
- **Description**: SQLite database operations
- **Configuration**: Points to `./data/test.db`

## How to Add More MCP Servers

Edit `mcp-servers.json` and add new server configurations:

```json
{
  "mcpServers": {
    "new-server": {
      "command": "npx",
      "args": ["-y", "@package/name", "...args"],
      "env": {
        "API_KEY": "${ENV_VAR_NAME}"
      },
      "enabled": true,
      "description": "What this server does"
    }
  }
}
```

## Available MCP Servers You Can Add

Popular MCP servers from the ecosystem:

1. **@modelcontextprotocol/server-postgres** - PostgreSQL database
2. **@modelcontextprotocol/server-slack** - Slack integration
3. **@modelcontextprotocol/server-gitlab** - GitLab API
4. **@modelcontextprotocol/server-google-drive** - Google Drive
5. **@modelcontextprotocol/server-puppeteer** - Browser automation
6. **@modelcontextprotocol/server-brave-search** - Brave search API
7. **@modelcontextprotocol/server-memory** - Persistent memory/notes
8. **@modelcontextprotocol/server-time** - Time/date utilities
9. **@modelcontextprotocol/server-everart** - AI image generation
10. **@modelcontextprotocol/server-fetch** - HTTP requests

## Testing Your Configuration

Run the test script to verify all servers are working:

```bash
cd server
node test-dynamic-mcp.js
```

This will:
- Load the configuration
- Connect to all enabled servers
- List all available tools
- Show connection status

## Architecture

```
Your C++ TUI Client
    ↓ TCP/JSON-RPC
MCP Bridge Server
    ├── Filesystem Server (stdio/MCP)
    ├── GitHub Server (stdio/MCP)
    ├── Serper Server (stdio/MCP)
    └── [Any other MCP servers...]
```

## Environment Variables

Make sure these are set for the servers you want to use:

- `GITHUB_TOKEN` - GitHub personal access token
- `SERPER_API_KEY` - Serper API key
- `OPENAI_API_KEY` - If using AI-powered servers
- `ANTHROPIC_API_KEY` - If using Claude-powered servers

## Troubleshooting

1. **Server fails to start**: Check if required environment variables are set
2. **Tools not appearing**: Verify server is enabled in config
3. **Connection errors**: Check npm/npx can download packages
4. **Permission errors**: Ensure filesystem paths are accessible

## Tool Naming Convention

All tools are prefixed with their server name:
- Filesystem tools: `filesystem_*`
- GitHub tools: `github_*`
- Serper tools: `serper_*`

This prevents naming conflicts when multiple servers provide similar functionality.