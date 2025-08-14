#!/usr/bin/env node

/**
 * Test script for agent file discovery
 */

import net from 'net';
import fs from 'fs';

// Silence console
const silentLog = (...args) => {
    fs.appendFileSync('.logs/test-agent.log', args.join(' ') + '\n');
};

function sendJsonRpcRequest(method, params) {
    return new Promise((resolve, reject) => {
        const client = new net.Socket();
        
        client.connect(3001, '127.0.0.1', () => {
            const request = {
                jsonrpc: '2.0',
                id: 1,
                method,
                params
            };
            
            const payload = JSON.stringify(request);
            const message = `Content-Length: ${Buffer.byteLength(payload)}\r\n\r\n${payload}`;
            
            silentLog(`Sending: ${method}`);
            client.write(message);
        });
        
        let buffer = '';
        client.on('data', (data) => {
            buffer += data.toString();
            
            // Check for complete message
            if (buffer.includes('\r\n\r\n')) {
                const parts = buffer.split('\r\n\r\n');
                if (parts.length >= 2) {
                    const jsonPart = parts[1];
                    try {
                        const response = JSON.parse(jsonPart);
                        client.end();
                        resolve(response);
                    } catch (err) {
                        // Continue reading
                    }
                }
            }
        });
        
        client.on('error', reject);
        
        client.setTimeout(30000, () => {
            client.destroy();
            reject(new Error('Request timeout'));
        });
    });
}

async function runTests() {
    try {
        // Test 1: List tools
        silentLog('\n=== Test 1: List Tools ===');
        const toolsResponse = await sendJsonRpcRequest('tools.list', {});
        silentLog(`Tools available: ${toolsResponse.result?.tools?.length || 0}`);
        
        // Test 2: File discovery with agent
        silentLog('\n=== Test 2: File Discovery with Agent ===');
        const fileResponse = await sendJsonRpcRequest('chat.send', {
            content: 'List all JavaScript files in the server directory',
            model: 'kimi'
        });
        
        silentLog(`Response received, length: ${fileResponse.result?.reply?.length || 0}`);
        
        // Check if response contains file information
        const reply = fileResponse.result?.reply || '';
        if (reply.includes('.js') || reply.includes('JavaScript')) {
            silentLog('SUCCESS: Agent found JavaScript files');
            silentLog(`Preview: ${reply.substring(0, 200)}...`);
        } else {
            silentLog('WARNING: Response may not contain file information');
        }
        
        // Test 3: Multi-step query
        silentLog('\n=== Test 3: Multi-step Query ===');
        const complexResponse = await sendJsonRpcRequest('chat.send', {
            content: 'Find all test files in the project and tell me how many there are',
            model: 'kimi'
        });
        
        silentLog(`Complex query response length: ${complexResponse.result?.reply?.length || 0}`);
        
        process.exit(0);
        
    } catch (err) {
        silentLog(`ERROR: ${err.message}`);
        process.exit(1);
    }
}

// Wait for server to be ready
setTimeout(runTests, 2000);