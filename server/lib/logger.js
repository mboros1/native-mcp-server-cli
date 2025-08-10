/**
 * @file logger.js
 * @brief File-based logging utilities (NO console output!)
 * @author Native MCP Team
 * @date 2025
 * 
 * CRITICAL: This project uses FTXUI for terminal UI.
 * ANY console.log/error/warn will corrupt the display.
 * ALL logging MUST go to files.
 */

import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const rootDir = path.join(__dirname, '../..');

// Ensure logs directory exists
const logsDir = path.join(rootDir, '.logs');
if (!fs.existsSync(logsDir)) {
    fs.mkdirSync(logsDir, { recursive: true });
}

// Create log streams for different components
const logStreams = {
    server: fs.createWriteStream(path.join(logsDir, 'mcp-bridge-server.log'), { flags: 'a' }),
    tools: fs.createWriteStream(path.join(logsDir, 'tools.log'), { flags: 'a' }),
    general: fs.createWriteStream(path.join(logsDir, 'app.log'), { flags: 'a' })
};

/**
 * Create a logger for a specific component
 * 
 * @param {string} component - Component name ('server', 'tools', 'general')
 * @returns {Function} Logger function
 */
export function createLogger(component = 'general') {
    const stream = logStreams[component] || logStreams.general;
    
    return function log(message, ...args) {
        const timestamp = new Date().toISOString();
        const formattedArgs = args.length > 0 ? ' ' + args.map(arg => 
            typeof arg === 'object' ? JSON.stringify(arg) : arg
        ).join(' ') : '';
        
        stream.write(`[${timestamp}] ${message}${formattedArgs}\n`);
    };
}

/**
 * Default loggers for common components
 */
export const serverLog = createLogger('server');
export const toolLog = createLogger('tools');
export const log = createLogger('general');

/**
 * Silence console methods in production
 * This prevents accidental console usage from breaking the UI
 */
export function silenceConsole() {
    const noop = () => {};
    
    // Only silence in production, not in tests
    if (process.env.NODE_ENV !== 'test') {
        console.log = noop;
        console.error = noop;
        console.warn = noop;
        console.info = noop;
        console.debug = noop;
    }
}

// Auto-silence on import (can be overridden in tests)
silenceConsole();