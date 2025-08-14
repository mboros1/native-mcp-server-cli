#!/usr/bin/env node

/**
 * Test script for dynamic MCP server configuration
 */

import { 
    initializeToolRouter, 
    getAvailableTools, 
    executeTool,
    getToolSystemStatus 
} from './tools/toolRouter-dynamic.js';

async function test() {
    console.log('Testing Dynamic MCP Server Configuration\n');
    console.log('='.repeat(50));
    
    try {
        // Initialize the tool router
        console.log('\n1. Initializing tool router with MCP servers...');
        const success = await initializeToolRouter();
        
        if (success) {
            console.log('✓ Tool router initialized successfully');
        } else {
            console.log('⚠️  Tool router initialized with warnings');
        }
        
        // Get system status
        console.log('\n2. Getting system status...');
        const status = await getToolSystemStatus();
        console.log('System Status:', JSON.stringify(status, null, 2));
        
        // List all available tools
        console.log('\n3. Listing all available tools...');
        const tools = getAvailableTools();
        console.log(`Found ${tools.length} tools:\n`);
        
        for (const tool of tools) {
            console.log(`  - ${tool.name}`);
            if (tool.server) {
                console.log(`    Server: ${tool.server}`);
            }
            console.log(`    Description: ${tool.description?.substring(0, 100)}...`);
        }
        
        // Test filesystem tool if available
        const filesystemTool = tools.find(t => t.name.includes('list_directory'));
        if (filesystemTool) {
            console.log(`\n4. Testing tool: ${filesystemTool.name}...`);
            try {
                const result = await executeTool(filesystemTool.name, { path: '.' });
                console.log('Result:', JSON.stringify(result, null, 2).substring(0, 500));
                console.log('✓ Tool execution successful');
            } catch (err) {
                console.error('✗ Tool execution failed:', err.message);
            }
        }
        
        // Test listing files with the static tool
        console.log('\n5. Testing static tool: list_files...');
        try {
            const result = await executeTool('list_files', { 
                path: '.', 
                recursive: false 
            });
            console.log('Files found:', result.files?.length || 0);
            console.log('✓ Static tool works');
        } catch (err) {
            console.error('✗ Static tool failed:', err.message);
        }
        
        console.log('\n' + '='.repeat(50));
        console.log('All tests completed!');
        console.log('\nYou can now add more MCP servers to mcp-servers.json');
        console.log('and they will be automatically loaded.');
        
        process.exit(0);
    } catch (err) {
        console.error('Test failed:', err);
        process.exit(1);
    }
}

// Run the test
test();