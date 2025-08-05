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
  timeout: 120000,
  headers: {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${process.env.KIMI_API_KEY}`,
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

// Chat history management
let chatHistory = [];
const chatHistoryFile = path.join(dataDir, 'chat-history.json');

function log(message) {
    const timestamp = new Date().toISOString();
    logFile.write(`[${timestamp}] ${message}\n`);
}

// Load chat history from file
function loadChatHistory() {
    if (fs.existsSync(chatHistoryFile)) {
        try {
            const data = fs.readFileSync(chatHistoryFile, 'utf8');
            chatHistory = data.trim().split('\n')
                .filter(line => line.trim())
                .map(line => JSON.parse(line));
            log(`Loaded ${chatHistory.length} messages from chat history`);
        } catch (err) {
            log(`Error loading chat history: ${err.message}`);
            chatHistory = [];
        }
    }
}

// Save message to chat history
function saveChatMessage(message) {
    chatHistory.push(message);
    try {
        fs.appendFileSync(chatHistoryFile, JSON.stringify(message) + '\n');
    } catch (err) {
        log(`Error saving chat message: ${err.message}`);
    }
}

// Rotate chat history (for /new command)
function rotateChatHistory() {
    if (fs.existsSync(chatHistoryFile)) {
        const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
        const backupFile = path.join(dataDir, `chat-history-${timestamp}.json`);
        try {
            fs.renameSync(chatHistoryFile, backupFile);
            log(`Rotated chat history to ${backupFile}`);
        } catch (err) {
            log(`Error rotating chat history: ${err.message}`);
        }
    }
    chatHistory = [];
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
          
          // Skip non-chat messages
          if (payload.type !== 'chat') {
            log(`Skipping non-chat message: ${payload.type}`);
            return;
          }
          
          // Add user message to chat history
          const userMessage = { role: 'user', content: payload.content };
          saveChatMessage(userMessage);
          
          // Build full conversation history for API call
          const messages = [...chatHistory];

          log(`Making API request to Kimi K2 with: ${payload.content}`);
          const { data: resp } = await moonshot.post('/v1/chat/completions', {
            model: 'kimi-k2',
            messages,
            temperature: 0.3,
            max_tokens: 1024,
          });

          const replyText = resp.choices[0].message.content;
          
          // Save assistant response to chat history
          const assistantMessage = { role: 'assistant', content: replyText };
          saveChatMessage(assistantMessage);
          
          const response = {
            type: 'response',
            original: payload,
            reply: replyText,
            timestamp: Date.now(),
          };

          socket.write(JSON.stringify(response) + '\n');
          log(`Sent reply to ${clientId}: ${replyText.slice(0, 100)}…`);

        } catch (err) {
          const errorMsg = err.response ? 
            `HTTP ${err.response.status}: ${JSON.stringify(err.response.data)}` : 
            err.message;
          
          socket.write(JSON.stringify({ type: 'error', message: errorMsg }) + '\n');
          log(`Error for ${clientId}: ${errorMsg}`);
          
          // Log full error details for debugging
          if (err.response) {
            log(`Response status: ${err.response.status}`);
            log(`Response headers: ${JSON.stringify(err.response.headers)}`);
            log(`Response data: ${JSON.stringify(err.response.data)}`);
          }
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
