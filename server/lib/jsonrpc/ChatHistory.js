/**
 * Chat History Manager for JSON-RPC Server
 * 
 * Manages in-memory chat history and coordinates with C++ client file storage
 * The C++ client is responsible for file persistence - this just maintains
 * the in-memory state for API calls.
 */

import fs from 'fs';
import path from 'path';
import { log } from '../logger.js';

/**
 * Chat History Manager
 * 
 * Handles:
 * - In-memory conversation history for API calls
 * - Reading from C++ client-written history files
 * - Coordinating with C++ for history management
 */
export class ChatHistory {
    constructor(dataDir = '.data') {
        this.dataDir = dataDir;
        this.messages = [];
        this.chatHistoryFile = path.join(dataDir, 'chat-history.json');
    }
    
    /**
     * Load chat history from C++ client file
     * The C++ client writes entries as newline-delimited JSON
     */
    loadFromFile() {
        if (fs.existsSync(this.chatHistoryFile)) {
            try {
                const data = fs.readFileSync(this.chatHistoryFile, 'utf8');
                const entries = data.trim().split('\n')
                    .filter(line => line.trim())
                    .map(line => JSON.parse(line));
                
                // Convert C++ ChatHistoryEntry format to API format
                this.messages = entries.map(entry => ({
                    role: entry.role,
                    content: entry.content
                    // Note: we ignore token_cnt and timestamp for API
                }));
                
                log(`Loaded ${this.messages.length} messages from C++ chat history`);
                return true;
            } catch (err) {
                log(`Error loading chat history: ${err.message}`);
                this.messages = [];
                return false;
            }
        }
        return false;
    }
    
    /**
     * Add a message to in-memory history
     * Note: C++ client handles file persistence
     * 
     * @param {string} role - 'user', 'assistant', or 'system'
     * @param {string} content - Message content
     */
    add(role, content) {
        this.messages.push({ role, content });
        log(`Added ${role} message to in-memory history (${this.messages.length} total)`);
    }
    
    /**
     * Get all messages
     * 
     * @returns {Array} - Message array
     */
    getMessages() {
        return [...this.messages];
    }
    
    /**
     * Get message count
     * 
     * @returns {number} - Number of messages
     */
    getMessageCount() {
        return this.messages.length;
    }
    
    /**
     * Get last user message
     * 
     * @returns {string|null} - Last user message content or null
     */
    getLastUserMessage() {
        for (let i = this.messages.length - 1; i >= 0; i--) {
            if (this.messages[i].role === 'user') {
                return this.messages[i].content;
            }
        }
        return null;
    }
    
    /**
     * Remove last user-assistant exchange
     */
    popLastExchange() {
        // Find last assistant message
        let assistantIndex = -1;
        for (let i = this.messages.length - 1; i >= 0; i--) {
            if (this.messages[i].role === 'assistant') {
                assistantIndex = i;
                break;
            }
        }
        
        if (assistantIndex === -1) {
            return;
        }
        
        // Find corresponding user message
        let userIndex = -1;
        for (let i = assistantIndex - 1; i >= 0; i--) {
            if (this.messages[i].role === 'user') {
                userIndex = i;
                break;
            }
        }
        
        if (userIndex !== -1) {
            // Remove both messages
            this.messages.splice(userIndex, assistantIndex - userIndex + 1);
            log(`Removed last exchange (${this.messages.length} messages remaining)`);
        }
    }
    
    /**
     * Clear in-memory messages
     * Note: C++ client handles file rotation
     */
    clear() {
        this.messages = [];
        log('Cleared in-memory chat history (C++ handles file rotation)');
    }
    
    /**
     * Reload history from C++ file
     * Called when C++ sends a reload request
     */
    reload() {
        return this.loadFromFile();
    }
    
    /**
     * Get sync stats for C++ client
     * Returns info about server-side history state
     */
    getSyncStats() {
        return {
            message_count: this.messages.length,
            last_role: this.messages.length > 0 ? this.messages[this.messages.length - 1].role : null
        };
    }
    
    /**
     * Get messages formatted for API
     * 
     * @returns {Array} - Messages array for API calls
     */
    getApiMessages() {
        return this.messages;
    }
}

// Create singleton instance
export const chatHistory = new ChatHistory();