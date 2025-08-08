import net from 'net';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';
import axios from 'axios';
import { config } from 'dotenv';

config();


const moonshot = axios.create({
  baseURL: 'https://kimi-k2.ai/api',
  timeout: 5*60000,
  headers: {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${process.env.KIMI_API_KEY}`,
  },
});

// --- new: openai axios instance ---------------------------------
const openai = axios.create({
  baseURL: 'https://api.openai.com',
  timeout: 5 * 60_000,
  headers: {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${process.env.OPENAI_API_KEY}`,
  },
});


// --- new: registry ----------------------------------------------
// +/** @typedef {'kimi' | 'o3'} ModelKey */   // simple typedef for JSDoc users
const MODEL_REGISTRY = Object.freeze({
  kimi: {
    id: 'kimi-k2',
    client: moonshot,
    endpoint: '/v1/chat/completions',
    extraParams: { temperature: 0.3, max_tokens: 1024 },
    formatRequest: (messages, params) => ({
      model: 'kimi-k2',
      messages,
      ...params
    })
  },
  o3: {
    id: 'o3',
    client: openai,
    endpoint: '/v1/responses',
    extraParams: {
      max_output_tokens: 1024,
      reasoning: { effort: process.env.O3_REASONING_EFFORT ?? 'medium' },
    },
    formatRequest: (messages, params, reasoning_effort) => {
      // Convert chat messages to simple string for o3 responses API
      const inputText = messages.map(msg => `${msg.role}: ${msg.content}`).join('\n\n');
      return {
        model: 'o3',
        input: inputText,
        text: { format: { type: 'text' } },
        reasoning: { 
          effort: reasoning_effort || params.reasoning?.effort || 'medium' 
        },
        max_output_tokens: params.max_output_tokens
      };
    }
  },
});


const __dirname = dirname(fileURLToPath(import.meta.url));

// Create .data directory if it doesn't exist
const dataDir = path.join(__dirname, '.data');
if (!fs.existsSync(dataDir)) {
    fs.mkdirSync(dataDir, { recursive: true });
}

// Create log file
const logFile = fs.createWriteStream(path.join(__dirname, 'test-server.log'), { flags: 'a' });

// Chat history management - now reading from C++ client
let chatHistory = [];
const chatHistoryFile = path.join(dataDir, 'chat-history.json');

// Client state management for mutex and retry functionality
const clientStates = new Map(); // clientId -> { processing: boolean, lastMessage: string, startTime: number }

// Track active requests with AbortControllers
const activeRequests = new Map(); // clientId -> { controller, startTime, message }

function log(message) {
    const timestamp = new Date().toISOString();
    logFile.write(`[${timestamp}] ${message}\n`);
}

// Client state management functions
function getClientState(clientId) {
    if (!clientStates.has(clientId)) {
        clientStates.set(clientId, { 
            processing: false, 
            lastMessage: null, 
            startTime: null 
        });
    }
    return clientStates.get(clientId);
}

function setClientProcessing(clientId, message) {
    const state = getClientState(clientId);
    state.processing = true;
    state.lastMessage = message;
    state.startTime = Date.now();
    log(`Client ${clientId} started processing: ${message.slice(0, 50)}...`);
}

function clearClientProcessing(clientId) {
    const state = getClientState(clientId);
    const duration = state.startTime ? Date.now() - state.startTime : 0;
    state.processing = false;
    state.startTime = null;
    log(`Client ${clientId} finished processing (${duration}ms)`);
    return duration;
}

function isClientProcessing(clientId) {
    return getClientState(clientId).processing;
}

// Load chat history from file (written by C++ client)
function loadChatHistory() {
    if (fs.existsSync(chatHistoryFile)) {
        try {
            const data = fs.readFileSync(chatHistoryFile, 'utf8');
            const entries = data.trim().split('\n')
                .filter(line => line.trim())
                .map(line => JSON.parse(line));
            
            // Convert C++ ChatHistoryEntry format to API format
            chatHistory = entries.map(entry => ({
                role: entry.role,
                content: entry.content
                // Note: we ignore token_cnt and timestamp - just need role/content for API
            }));
            
            log(`Loaded ${chatHistory.length} messages from chat history`);
        } catch (err) {
            log(`Error loading chat history: ${err.message}`);
            chatHistory = [];
        }
    }
}

// Add message to in-memory chat history (C++ handles file writing)
function addToMemoryHistory(role, content) {
    chatHistory.push({ role, content });
}

// Rotate chat history (for /new command) - just clear memory, C++ handles file rotation
function rotateChatHistory() {
    chatHistory = [];
    log('Cleared chat history from memory (C++ handles file rotation)');
}

// Main chat processing function with timeout handling
async function processChatMessage(socket, clientId, messageContent, clientTimeout = null, modelKey = 'kimi', reasoning_effort = null) {
    // Check if client is already processing a message
    if (isClientProcessing(clientId)) {
        socket.write(JSON.stringify({
            type: 'error',
            message: 'Previous request still processing. Please wait or use retry if it timed out.'
        }) + '\n');
        return;
    }
    
    // Set processing state
    setClientProcessing(clientId, messageContent);
    
    // Create AbortController for this request
    const controller = new AbortController();
    const signal = controller.signal;
    
    // Store the active request
    activeRequests.set(clientId, {
        controller,
        startTime: Date.now(),
        message: messageContent
    });
    
    try {
        // Add user message to in-memory history (C++ already wrote to file)
        addToMemoryHistory('user', messageContent);
        
        // Build full conversation history for API call
        const messages = [...chatHistory];

        // Use client-provided timeout or default to 5 minutes
        const timeoutMs = clientTimeout || (5 * 60 * 1000);
        
        // Get model configuration from registry
        const modelConfig = MODEL_REGISTRY[modelKey];
        if (!modelConfig) {
            throw new Error(`Unknown model: ${modelKey}`);
        }
        
        log(`Making API request to ${modelConfig.id} with: ${messageContent}`);
        log(`DEBUG: Starting request for ${clientId}, signal.aborted = ${signal.aborted}`);
        
        // Check if already aborted before making request
        if (signal.aborted) {
            log(`DEBUG: Request was already aborted before API call for ${clientId}`);
            throw new Error('Request was aborted');
        }
        
        // Build request parameters using model-specific formatter
        const requestParams = modelConfig.formatRequest(messages, modelConfig.extraParams, reasoning_effort);
        
        const { data: resp } = await modelConfig.client.post(modelConfig.endpoint, requestParams, {
            signal, // Pass abort signal to axios
            timeout: timeoutMs
        });
        
        // Check if the request was cancelled while in progress
        if (signal.aborted) {
            log(`DEBUG: Request was cancelled while in progress for ${clientId} - discarding response`);
            socket.write(JSON.stringify({
                type: 'system',
                message: 'Request was cancelled (response discarded)'
            }) + '\n');
            return;
        }
        
        // Parse response based on model type
        let replyText;
        if (modelKey === 'o3') {
            // OpenAI responses API format: find the message item in output array
            const messageItem = resp.output?.find(item => item.type === 'message');
            replyText = messageItem?.content?.[0]?.text || 'No response content';
        } else {
            // Traditional chat completions format (Kimi)
            replyText = resp.choices[0].message.content;
        }
        
        // Add assistant response to in-memory history (C++ will write to file when it receives response)
        addToMemoryHistory('assistant', replyText);
        
        const response = {
            type: 'response',
            original: { content: messageContent },
            reply: replyText,
            timestamp: Date.now(),
        };

        socket.write(JSON.stringify(response) + '\n');
        log(`Sent reply to ${clientId}: ${replyText.slice(0, 100)}…`);
        
    } catch (err) {
        log(`DEBUG: Caught error for ${clientId}: ${err.name}, signal.aborted = ${signal.aborted}`);
        
        // Check if this was an abort/cancellation
        if (err.name === 'AbortError' || signal.aborted) {
            log(`Request cancelled for ${clientId}`);
            socket.write(JSON.stringify({
                type: 'system',
                message: 'Request was cancelled'
            }) + '\n');
            return;
        }
        
        const duration = clearClientProcessing(clientId);
        
        // Check if this was a timeout error
        const isTimeout = err.code === 'ECONNABORTED' || 
                         err.code === 'ETIMEDOUT' || 
                         (err.response && err.response.status === 408) ||
                         duration >= (5 * 60 * 1000 - 1000); // Close to our 5min timeout
        
        if (isTimeout) {
            const timeoutSeconds = Math.round(duration / 1000);
            const timeoutMessage = `Request timed out after ${timeoutSeconds}s`;
            
            socket.write(JSON.stringify({
                type: 'timeout_error',
                message: timeoutMessage,
                duration: duration,
                canRetry: true,
                originalMessage: messageContent
            }) + '\n');
            
            log(`Timeout for ${clientId} after ${timeoutSeconds}s: ${messageContent.slice(0, 50)}...`);
        } else {
            // Regular error handling
            const errorMsg = err.response ? 
                `HTTP ${err.response.status}: ${JSON.stringify(err.response.data)}` : 
                err.message;
            
            socket.write(JSON.stringify({ 
                type: 'error', 
                message: errorMsg 
            }) + '\n');
            log(`Error for ${clientId}: ${errorMsg}`);
            
            // Log full error details for debugging
            if (err.response) {
                log(`Response status: ${err.response.status}`);
                log(`Response headers: ${JSON.stringify(err.response.headers)}`);
                log(`Response data: ${JSON.stringify(err.response.data)}`);
            }
        }
    } finally {
        // Clean up active request and processing state
        activeRequests.delete(clientId);
        clearClientProcessing(clientId);
    }
}

// Handle interrupt requests (reset/cancel)
function handleInterrupt(socket, clientId) {
    const activeRequest = activeRequests.get(clientId);
    
    if (activeRequest) {
        log(`DEBUG: Found active request for ${clientId}, calling abort()`);
        
        // Cancel the in-flight request
        activeRequest.controller.abort();
        activeRequests.delete(clientId);
        
        log(`DEBUG: Abort called, signal.aborted = ${activeRequest.controller.signal.aborted}`);
        log(`Cancelled active request for ${clientId}`);
        
        socket.write(JSON.stringify({
            type: 'system',
            message: 'In-flight request cancelled and state cleared'
        }) + '\n');
    } else {
        log(`DEBUG: No active request found for ${clientId}`);
        socket.write(JSON.stringify({
            type: 'system', 
            message: 'No active request to cancel'
        }) + '\n');
    }
    
    // Clear processing state either way
    clearClientProcessing(clientId);
}

// Handle retry requests
async function handleRetryRequest(socket, clientId, originalMessage, modelKey = 'kimi', reasoning_effort = null) {
    // If no original message provided, try to find last user message from chat history
    if (!originalMessage) {
        // Find the last user message from chat history in memory
        for (let i = chatHistory.length - 1; i >= 0; i--) {
            if (chatHistory[i].role === 'user') {
                originalMessage = chatHistory[i].content;
                break;
            }
        }
        
        if (!originalMessage) {
            socket.write(JSON.stringify({
                type: 'error',
                message: 'No user message found to retry'
            }) + '\n');
            return;
        }
    }
    
    log(`Retry request from ${clientId} for message: ${originalMessage.slice(0, 50)}...`);
    
    // Remove last user message from memory history (assuming it was added before the timeout)
    if (chatHistory.length > 0 && chatHistory[chatHistory.length - 1].role === 'user') {
        chatHistory.pop();
        log(`Removed last user message from chat history for retry`);
    }
    
    // Process the retry as a normal chat message with preserved model settings
    await processChatMessage(socket, clientId, originalMessage, null, modelKey, reasoning_effort);
}

const server = net.createServer((socket) => {
    const clientId = `${socket.remoteAddress}:${socket.remotePort}`;
    log(`Client connected: ${clientId}`);

    // Send a welcome message
    socket.write(JSON.stringify({ type: 'welcome', message: 'Connected to test server' }) + '\n');

    socket.on('data', (data) => {
      const lines = data.toString().trim().split('\n');
      lines.forEach(async (msg) => {
        if (!msg) return;
        try {
          const payload = JSON.parse(msg);
          log(`Received payload: ${JSON.stringify(payload)}`);
          
          // Handle /new command for chat history rotation
          if (payload.type === 'chat' && payload.content.trim() === '/new') {
            const startTime = Date.now();
            log(`Processing /new command for ${clientId} at ${new Date().toISOString()}`);
            
            rotateChatHistory();
            
            const response = JSON.stringify({ 
              type: 'response', 
              original: payload,
              reply: 'Started new conversation. Chat history has been rotated.',
              timestamp: Date.now()
            }) + '\n';
            
            socket.write(response);
            
            const endTime = Date.now();
            log(`Chat history rotated for ${clientId} in ${endTime - startTime}ms`);
            return;
          }
          
          // Handle reload request
          if (payload.type === 'reload' && payload.content === 'chat_history') {
            log(`Reload request from ${clientId} - reloading chat history from file`);
            loadChatHistory();
            log(`Reloaded chat history: ${chatHistory.length} entries`);
            return;
          }
          
          // Handle sync request  
          if (payload.type === 'sync' && payload.content === 'request_history') {
            const serverStats = `Server: ${chatHistory.length} entries`;
            socket.write(JSON.stringify({
              type: 'sync_response',
              server_stats: serverStats,
              server_history: chatHistory,
              timestamp: Date.now()
            }) + '\n');
            log(`Sent sync response to ${clientId}: ${serverStats}`);
            return;
          }
          
          // Handle retry requests
          if (payload.type === 'retry') {
            await handleRetryRequest(socket, clientId, payload.originalMessage, payload.model || 'kimi', payload.reasoning_effort);
            return;
          }
          
          // Handle reset/interrupt requests
          if (payload.type === 'reset') {
            log(`Reset/interrupt request from ${clientId}`);
            handleInterrupt(socket, clientId);
            return;
          }
          
          // Skip non-chat/retry/reset messages  
          if (payload.type !== 'chat') {
            log(`Skipping non-chat message: ${payload.type}`);
            return;
          }
          
          // Process chat message with timeout handling
          await processChatMessage(socket, clientId, payload.content, payload.timeout, payload.model || 'kimi', payload.reasoning_effort);

        } catch (err) {
          log(`Error parsing message from ${clientId}: ${err.message}`);
        }
      });
    });

    // Send dummy messages every 5 minutes
    const interval = setInterval(() => {
        const dummyMsg = {
            type: 'heartbeat',
            message: 'Server is alive',
            timestamp: Date.now()
        };
        socket.write(JSON.stringify(dummyMsg) + '\n');
        log(`Sent heartbeat to ${clientId}`);
    }, 300000);

    socket.on('end', () => {
        log(`Client disconnected: ${clientId}`);
        clearInterval(interval);
    });

    socket.on('error', (err) => {
        log(`Socket error for ${clientId}: ${err.message}`);
        clearInterval(interval);
    });
});

export async function startServer(port = 4000) {
    // Load chat history at startup
    loadChatHistory();
    
    return new Promise((resolve, reject) => {
        server.listen(port, '127.0.0.1', () => {
            log(`Test server listening on 127.0.0.1:${port}`);
            resolve(server);
        });
        
        server.on('error', (err) => {
            log(`Server error: ${err.message}`);
            reject(err);
        });
    });
}

export function stopServer() {
    return new Promise((resolve) => {
        log('Server shutting down...');
        server.close(() => {
            logFile.end();
            resolve();
        });
    });
}
