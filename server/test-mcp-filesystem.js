#!/usr/bin/env node

/**
 * Test script for MCP filesystem server integration
 */

import { initializeMCPFilesystem, mcpListDirectory, mcpReadFile } from './tools/mcpFilesystem.js';

async function test() {
    console.log('Testing MCP Filesystem Server Integration\n');
    
    try {
        // Initialize connection
        console.log('1. Connecting to MCP filesystem server...');
        await initializeMCPFilesystem();
        console.log('✓ Connected successfully\n');
        
        // Test listing directory
        console.log('2. Testing list_directory...');
        const listResult = await mcpListDirectory({ path: '.' });
        console.log('Directory contents:', JSON.stringify(listResult, null, 2));
        console.log('✓ List directory works\n');
        
        // Test reading a file
        console.log('3. Testing read_file (reading mcp-bridge-server.js)...');
        const readResult = await mcpReadFile({ path: 'mcp-bridge-server.js' });
        const content = readResult.content?.[0]?.text || JSON.stringify(readResult);
        console.log('File content (first 200 chars):', content.substring(0, 200));
        console.log('✓ Read file works\n');
        
        console.log('All tests passed! MCP filesystem server integration is working.');
        process.exit(0);
    } catch (err) {
        console.error('Test failed:', err);
        process.exit(1);
    }
}

test();