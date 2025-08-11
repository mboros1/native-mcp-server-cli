import fs from 'fs';
import path from 'path';

/**
 * Chat History Manager
 * Handles loading, saving, and managing chat conversation history
 */
class ChatHistoryManager {
    constructor(dataDir) {
        this.chatHistory = [];
        this.chatHistoryFile = path.join(dataDir, 'chat-history.json');
        this.rotatedHistoryFile = path.join(dataDir, 'chat-history-rotated.json');
    }

    /**
     * Load chat history from file
     * Supports both JSON array format and NDJSON (newline-delimited) format
     */
    load() {
        try {
            if (fs.existsSync(this.chatHistoryFile)) {
                const data = fs.readFileSync(this.chatHistoryFile, 'utf8');
                
                // Try to parse as NDJSON first (C++ client format)
                if (data.includes('\n') && !data.trim().startsWith('[')) {
                    const entries = data.trim().split('\n')
                        .filter(line => line.trim())
                        .map(line => {
                            const entry = JSON.parse(line);
                            // Convert C++ ChatHistoryEntry format to API format
                            return {
                                role: entry.role,
                                content: entry.content
                                // Ignore token_cnt and timestamp
                            };
                        });
                    this.chatHistory = entries;
                } else {
                    // Try as regular JSON array
                    this.chatHistory = JSON.parse(data);
                }
                
                return this.chatHistory.length;
            }
        } catch (error) {
            // Silently handle errors per project requirements
            this.chatHistory = [];
        }
        return 0;
    }

    /**
     * Add message to in-memory chat history
     * @param {string} role - 'user', 'assistant', or 'system'
     * @param {string|null} content - Message content
     * @param {Array|null} toolCalls - Tool calls if any
     */
    addMessage(role, content, toolCalls = null) {
        if (toolCalls) {
            this.chatHistory.push({ role, content: null, tool_calls: toolCalls });
        } else {
            this.chatHistory.push({ role, content });
        }
    }

    /**
     * Get current chat history
     */
    getHistory() {
        return this.chatHistory;
    }

    /**
     * Get history length
     */
    getLength() {
        return this.chatHistory.length;
    }

    /**
     * Remove last message if it matches the role
     * @param {string} role - Role to match
     * @returns {boolean} - True if removed
     */
    removeLastIfRole(role) {
        if (this.chatHistory.length > 0 && 
            this.chatHistory[this.chatHistory.length - 1].role === role) {
            this.chatHistory.pop();
            return true;
        }
        return false;
    }

    /**
     * Rotate chat history - clear memory (C++ handles file rotation)
     */
    rotate() {
        this.chatHistory = [];
    }

    /**
     * Sync with provided history
     * @param {Array} newHistory - New history to sync with
     */
    sync(newHistory) {
        this.chatHistory = newHistory;
    }
}

export default ChatHistoryManager;