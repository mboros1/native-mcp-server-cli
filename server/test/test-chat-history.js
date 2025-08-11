import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import ChatHistoryManager from '../lib/chatHistory.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const testDataDir = path.join(__dirname, 'test-data');

describe('ChatHistoryManager', () => {
    let chatHistory;
    
    beforeEach(() => {
        // Create test data directory
        if (!fs.existsSync(testDataDir)) {
            fs.mkdirSync(testDataDir, { recursive: true });
        }
        chatHistory = new ChatHistoryManager(testDataDir);
    });
    
    afterEach(() => {
        // Clean up test data
        if (fs.existsSync(testDataDir)) {
            fs.rmSync(testDataDir, { recursive: true });
        }
    });
    
    it('should initialize with empty history', () => {
        assert.strictEqual(chatHistory.getLength(), 0);
        assert.deepStrictEqual(chatHistory.getHistory(), []);
    });
    
    it('should add user messages correctly', () => {
        chatHistory.addMessage('user', 'Hello, AI!');
        assert.strictEqual(chatHistory.getLength(), 1);
        
        const history = chatHistory.getHistory();
        assert.strictEqual(history[0].role, 'user');
        assert.strictEqual(history[0].content, 'Hello, AI!');
    });
    
    it('should add assistant messages correctly', () => {
        chatHistory.addMessage('assistant', 'Hello! How can I help you?');
        assert.strictEqual(chatHistory.getLength(), 1);
        
        const history = chatHistory.getHistory();
        assert.strictEqual(history[0].role, 'assistant');
        assert.strictEqual(history[0].content, 'Hello! How can I help you?');
    });
    
    it('should add tool calls correctly', () => {
        const toolCalls = [{
            id: 'call_123',
            type: 'function',
            function: {
                name: 'list_files',
                arguments: '{"path": "/"}'
            }
        }];
        
        chatHistory.addMessage('assistant', null, toolCalls);
        assert.strictEqual(chatHistory.getLength(), 1);
        
        const history = chatHistory.getHistory();
        assert.strictEqual(history[0].role, 'assistant');
        assert.strictEqual(history[0].content, null);
        assert.deepStrictEqual(history[0].tool_calls, toolCalls);
    });
    
    it('should maintain conversation order', () => {
        chatHistory.addMessage('user', 'What is 2+2?');
        chatHistory.addMessage('assistant', '2+2 equals 4.');
        chatHistory.addMessage('user', 'What about 3+3?');
        chatHistory.addMessage('assistant', '3+3 equals 6.');
        
        const history = chatHistory.getHistory();
        assert.strictEqual(history.length, 4);
        assert.strictEqual(history[0].content, 'What is 2+2?');
        assert.strictEqual(history[1].content, '2+2 equals 4.');
        assert.strictEqual(history[2].content, 'What about 3+3?');
        assert.strictEqual(history[3].content, '3+3 equals 6.');
    });
    
    it('should remove last message if role matches', () => {
        chatHistory.addMessage('user', 'First message');
        chatHistory.addMessage('assistant', 'Response');
        chatHistory.addMessage('user', 'Second message');
        
        // Remove last user message
        const removed = chatHistory.removeLastIfRole('user');
        assert.strictEqual(removed, true);
        assert.strictEqual(chatHistory.getLength(), 2);
        
        // Try to remove assistant message (should fail)
        const notRemoved = chatHistory.removeLastIfRole('user');
        assert.strictEqual(notRemoved, false);
        assert.strictEqual(chatHistory.getLength(), 2);
    });
    
    it('should rotate (clear) history', () => {
        chatHistory.addMessage('user', 'Message 1');
        chatHistory.addMessage('assistant', 'Response 1');
        assert.strictEqual(chatHistory.getLength(), 2);
        
        chatHistory.rotate();
        assert.strictEqual(chatHistory.getLength(), 0);
        assert.deepStrictEqual(chatHistory.getHistory(), []);
    });
    
    it('should load history from file', () => {
        // Create a test history file
        const testHistory = [
            { role: 'user', content: 'Loaded message 1' },
            { role: 'assistant', content: 'Loaded response 1' }
        ];
        const historyFile = path.join(testDataDir, 'chat-history.json');
        fs.writeFileSync(historyFile, JSON.stringify(testHistory));
        
        // Load it
        const count = chatHistory.load();
        assert.strictEqual(count, 2);
        assert.deepStrictEqual(chatHistory.getHistory(), testHistory);
    });
    
    it('should handle missing history file gracefully', () => {
        const count = chatHistory.load();
        assert.strictEqual(count, 0);
        assert.deepStrictEqual(chatHistory.getHistory(), []);
    });
    
    it('should handle corrupted history file gracefully', () => {
        const historyFile = path.join(testDataDir, 'chat-history.json');
        fs.writeFileSync(historyFile, 'not valid json');
        
        const count = chatHistory.load();
        assert.strictEqual(count, 0);
        assert.deepStrictEqual(chatHistory.getHistory(), []);
    });
    
    it('should sync with provided history', () => {
        const newHistory = [
            { role: 'user', content: 'Synced message' },
            { role: 'assistant', content: 'Synced response' }
        ];
        
        chatHistory.sync(newHistory);
        assert.strictEqual(chatHistory.getLength(), 2);
        assert.deepStrictEqual(chatHistory.getHistory(), newHistory);
    });
});

// Test that history is properly formatted for LLM
describe('ChatHistory LLM Preparation', () => {
    let chatHistory;
    
    beforeEach(() => {
        if (!fs.existsSync(testDataDir)) {
            fs.mkdirSync(testDataDir, { recursive: true });
        }
        chatHistory = new ChatHistoryManager(testDataDir);
    });
    
    afterEach(() => {
        if (fs.existsSync(testDataDir)) {
            fs.rmSync(testDataDir, { recursive: true });
        }
    });
    
    it('should preserve all messages for LLM context', () => {
        // Simulate a conversation
        chatHistory.addMessage('user', 'Remember the number 42');
        chatHistory.addMessage('assistant', 'I will remember the number 42.');
        chatHistory.addMessage('user', 'What number did I ask you to remember?');
        
        // Get history as it would be sent to LLM
        const history = chatHistory.getHistory();
        
        // Verify all messages are present
        assert.strictEqual(history.length, 3);
        assert.strictEqual(history[0].role, 'user');
        assert.strictEqual(history[0].content, 'Remember the number 42');
        assert.strictEqual(history[1].role, 'assistant');
        assert.strictEqual(history[1].content, 'I will remember the number 42.');
        assert.strictEqual(history[2].role, 'user');
        assert.strictEqual(history[2].content, 'What number did I ask you to remember?');
    });
    
    it('should include tool calls in history', () => {
        chatHistory.addMessage('user', 'List files in the current directory');
        
        const toolCalls = [{
            id: 'call_abc',
            type: 'function',
            function: {
                name: 'list_files',
                arguments: '{"path": "."}'
            }
        }];
        chatHistory.addMessage('assistant', null, toolCalls);
        
        // Tool results would be added as separate messages
        chatHistory.addMessage('tool', JSON.stringify({
            entries: ['file1.txt', 'file2.txt']
        }));
        
        chatHistory.addMessage('assistant', 'I found 2 files: file1.txt and file2.txt');
        
        const history = chatHistory.getHistory();
        assert.strictEqual(history.length, 4);
        
        // Verify tool call is preserved
        assert.strictEqual(history[1].role, 'assistant');
        assert.strictEqual(history[1].content, null);
        assert.deepStrictEqual(history[1].tool_calls, toolCalls);
    });
});