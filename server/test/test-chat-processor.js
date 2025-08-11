import { describe, it, beforeEach, afterEach } from 'node:test';
import assert from 'node:assert';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import ChatHistoryManager from '../lib/chatHistory.js';
import ChatProcessor from '../lib/chatProcessor.js';
import APIClientManager from '../lib/apiClientManager.js';

const __dirname = dirname(fileURLToPath(import.meta.url));
const testDataDir = path.join(__dirname, 'test-data');

// Mock logger
const mockLogger = (msg) => {};

// Mock socket
class MockSocket {
    constructor() {
        this.sentMessages = [];
    }
    
    write(data) {
        this.sentMessages.push(data);
    }
    
    getLastMessage() {
        return this.sentMessages[this.sentMessages.length - 1];
    }
    
    getAllMessages() {
        return this.sentMessages;
    }
    
    clear() {
        this.sentMessages = [];
    }
}

// Mock API Manager
class MockAPIManager extends APIClientManager {
    constructor() {
        super();
        this.lastRequest = null;
        this.mockResponse = null;
    }
    
    setMockResponse(response) {
        this.mockResponse = response;
    }
    
    createRequest(modelKey, messages, params = {}, tools = null) {
        this.lastRequest = { modelKey, messages, params, tools };
        return { messages, ...params };
    }
    
    async callAPI(modelKey, requestData) {
        // Simulate API delay
        await new Promise(resolve => setTimeout(resolve, 10));
        
        if (this.mockResponse) {
            return this.mockResponse;
        }
        
        // Default mock response
        return {
            choices: [{
                message: {
                    content: 'Mock response to: ' + requestData.messages[requestData.messages.length - 1].content
                }
            }]
        };
    }
    
    getModel(key) {
        if (key === 'test') {
            return {
                id: 'test-model',
                supportsTools: false
            };
        }
        return null;
    }
}

describe('ChatProcessor', () => {
    let chatHistory;
    let apiManager;
    let chatProcessor;
    let mockSocket;
    
    beforeEach(() => {
        if (!fs.existsSync(testDataDir)) {
            fs.mkdirSync(testDataDir, { recursive: true });
        }
        
        chatHistory = new ChatHistoryManager(testDataDir);
        apiManager = new MockAPIManager();
        chatProcessor = new ChatProcessor(apiManager, chatHistory, mockLogger);
        mockSocket = new MockSocket();
    });
    
    afterEach(() => {
        if (fs.existsSync(testDataDir)) {
            fs.rmSync(testDataDir, { recursive: true });
        }
    });
    
    it('should include system prompt in messages', async () => {
        await chatProcessor.processMessage(mockSocket, 'test-client', 'Hello', null, 'test');
        
        // Check that system prompt was included
        const request = apiManager.lastRequest;
        assert(request);
        assert(request.messages.length >= 2); // System + user
        assert.strictEqual(request.messages[0].role, 'system');
        assert(request.messages[0].content.includes('helpful AI assistant'));
    });
    
    it('should include chat history in context', async () => {
        // Add some history
        chatHistory.addMessage('user', 'Remember the number 42');
        chatHistory.addMessage('assistant', 'I will remember the number 42.');
        
        // Process new message
        await chatProcessor.processMessage(
            mockSocket, 
            'test-client', 
            'What number did I ask you to remember?',
            null,
            'test'
        );
        
        // Check that all history was included
        const request = apiManager.lastRequest;
        assert(request);
        assert(request.messages.length === 4); // System + 2 history + 1 new
        
        // Verify order
        assert.strictEqual(request.messages[0].role, 'system');
        assert.strictEqual(request.messages[1].role, 'user');
        assert.strictEqual(request.messages[1].content, 'Remember the number 42');
        assert.strictEqual(request.messages[2].role, 'assistant');
        assert.strictEqual(request.messages[2].content, 'I will remember the number 42.');
        assert.strictEqual(request.messages[3].role, 'user');
        assert.strictEqual(request.messages[3].content, 'What number did I ask you to remember?');
    });
    
    it('should preserve conversation continuity', async () => {
        // Simulate a conversation
        await chatProcessor.processMessage(mockSocket, 'client1', 'My name is Alice', null, 'test');
        
        // Set mock response for next call
        apiManager.setMockResponse({
            choices: [{
                message: {
                    content: 'Hello Alice! I remember your name from our conversation.'
                }
            }]
        });
        
        await chatProcessor.processMessage(mockSocket, 'client1', 'What is my name?', null, 'test');
        
        // Check that second request includes first exchange
        const request = apiManager.lastRequest;
        const messages = request.messages;
        
        // Should have: system, user (Alice), assistant (response), user (what's my name)
        assert(messages.length >= 4);
        
        // Verify the conversation flow is preserved
        const userMessages = messages.filter(m => m.role === 'user');
        assert.strictEqual(userMessages[0].content, 'My name is Alice');
        assert.strictEqual(userMessages[1].content, 'What is my name?');
    });
    
    it('should handle timeout correctly', async () => {
        // Make API manager delay longer than timeout
        apiManager.callAPI = async () => {
            await new Promise(resolve => setTimeout(resolve, 200));
            throw new Error('Should have timed out');
        };
        
        await chatProcessor.processMessage(mockSocket, 'client1', 'Test timeout', 100, 'test');
        
        // Check error was sent
        const lastMessage = mockSocket.getLastMessage();
        assert(lastMessage.includes('timeout'));
        
        // Check message was removed from history
        assert.strictEqual(chatHistory.getLength(), 0);
    });
});

describe('ChatHistory NDJSON Format', () => {
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
    
    it('should load NDJSON format from C++ client', () => {
        // Create NDJSON file like C++ client does
        const historyFile = path.join(testDataDir, 'chat-history.json');
        const ndjsonContent = [
            '{"role":"user","content":"First message","token_cnt":3,"timestamp":1234567890}',
            '{"role":"assistant","content":"First response","token_cnt":2,"timestamp":1234567891}',
            '{"role":"user","content":"Second message","token_cnt":2,"timestamp":1234567892}'
        ].join('\n');
        
        fs.writeFileSync(historyFile, ndjsonContent);
        
        // Load it
        const count = chatHistory.load();
        assert.strictEqual(count, 3);
        
        // Verify content
        const history = chatHistory.getHistory();
        assert.strictEqual(history[0].role, 'user');
        assert.strictEqual(history[0].content, 'First message');
        assert.strictEqual(history[1].role, 'assistant');
        assert.strictEqual(history[1].content, 'First response');
        assert.strictEqual(history[2].role, 'user');
        assert.strictEqual(history[2].content, 'Second message');
        
        // Verify extra fields were stripped
        assert.strictEqual(history[0].token_cnt, undefined);
        assert.strictEqual(history[0].timestamp, undefined);
    });
    
    it('should still load regular JSON array format', () => {
        const historyFile = path.join(testDataDir, 'chat-history.json');
        const jsonContent = JSON.stringify([
            { role: 'user', content: 'Test message' },
            { role: 'assistant', content: 'Test response' }
        ]);
        
        fs.writeFileSync(historyFile, jsonContent);
        
        const count = chatHistory.load();
        assert.strictEqual(count, 2);
        
        const history = chatHistory.getHistory();
        assert.strictEqual(history[0].content, 'Test message');
        assert.strictEqual(history[1].content, 'Test response');
    });
});