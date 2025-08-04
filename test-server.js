import net from 'net';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));

// Create log file
const logFile = fs.createWriteStream(path.join(__dirname, 'test-server.log'), { flags: 'a' });

function log(message) {
    const timestamp = new Date().toISOString();
    logFile.write(`[${timestamp}] ${message}\n`);
}

const server = net.createServer((socket) => {
    const clientId = `${socket.remoteAddress}:${socket.remotePort}`;
    log(`Client connected: ${clientId}`);

    // Send a welcome message
    socket.write(JSON.stringify({ type: 'welcome', message: 'Connected to test server' }) + '\n');

    // Echo handler
    socket.on('data', (data) => {
        const messages = data.toString().trim().split('\n');
        messages.forEach(msg => {
            if (msg) {
                log(`Received from ${clientId}: ${msg}`);
                try {
                    const parsed = JSON.parse(msg);
                    const response = {
                        type: 'echo',
                        original: parsed,
                        timestamp: Date.now()
                    };
                    socket.write(JSON.stringify(response) + '\n');
                    log(`Sent echo to ${clientId}: ${JSON.stringify(response)}`);
                } catch (e) {
                    log(`Error parsing message from ${clientId}: ${e.message}`);
                }
            }
        });
    });

    // Send dummy messages every 3 seconds
    const interval = setInterval(() => {
        const dummyMsg = {
            type: 'heartbeat',
            message: 'Server is alive',
            timestamp: Date.now()
        };
        socket.write(JSON.stringify(dummyMsg) + '\n');
        log(`Sent heartbeat to ${clientId}`);
    }, 3000);

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