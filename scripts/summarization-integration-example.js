import ConversationSummarizer from './summarization.js';
import fs from 'fs';
import path from 'path';

/**
 * Integration example showing how to use ConversationSummarizer 
 * with your existing MCP server and chat history system
 */

class ChatHistoryManager {
  constructor() {
    this.summarizer = new ConversationSummarizer({
      triggerTokens: 40000,    // Summarize at 40k tokens
      targetSummaryTokens: 4000, // Target 4k token summaries  
      cheapModel: 'gpt-4.1-nano',   // Use cheapest for parallel summaries
      qualityModel: 'gpt-4.1',      // Use quality model for final selection
      logger: console
    });
    
    this.dataDir = '.data';
    this.chatHistoryFile = path.join(this.dataDir, 'chat-history.json');
    this.conversationLog = [];
    
    // Ensure data directory exists
    if (!fs.existsSync(this.dataDir)) {
      fs.mkdirSync(this.dataDir, { recursive: true });
    }
    
    this.loadChatHistory();
  }

  /**
   * Load existing chat history from file
   */
  loadChatHistory() {
    if (!fs.existsSync(this.chatHistoryFile)) {
      console.log('No existing chat history found');
      return;
    }

    try {
      const fileContent = fs.readFileSync(this.chatHistoryFile, 'utf8');
      const lines = fileContent.trim().split('\n').filter(line => line.trim());
      
      this.conversationLog = lines.map(line => JSON.parse(line));
      console.log(`Loaded ${this.conversationLog.length} chat history entries`);
      
    } catch (error) {
      console.error('Error loading chat history:', error.message);
      this.conversationLog = [];
    }
  }

  /**
   * Add new message to chat history
   */
  addMessage(role, content) {
    const entry = {
      role,
      content,
      token_cnt: this.estimateTokens(content),
      timestamp: new Date().toISOString()
    };
    
    this.conversationLog.push(entry);
    this.saveChatHistory();
    
    // Check if we need to summarize
    this.checkAndSummarize();
  }

  /**
   * Save chat history to file (NDJSON format)
   */
  saveChatHistory() {
    try {
      const ndjsonContent = this.conversationLog
        .map(entry => JSON.stringify(entry))
        .join('\n');
      
      fs.writeFileSync(this.chatHistoryFile, ndjsonContent + '\n');
    } catch (error) {
      console.error('Error saving chat history:', error.message);
    }
  }

  /**
   * Get total token count for current conversation
   */
  getTotalTokens() {
    return this.conversationLog.reduce((sum, entry) => sum + (entry.token_cnt || 0), 0);
  }

  /**
   * Convert conversation log to text format for summarization
   */
  getConversationText() {
    return this.conversationLog
      .map(entry => `${entry.role}: ${entry.content}`)
      .join('\n\n');
  }

  /**
   * Check if conversation needs summarization and perform if needed
   */
  async checkAndSummarize() {
    const conversationText = this.getConversationText();
    
    if (this.summarizer.shouldSummarize(conversationText)) {
      console.log('🔄 Token limit reached, starting summarization...');
      
      try {
        const result = await this.summarizer.summarizeConversation(conversationText);
        
        // Replace conversation history with summary
        await this.applySummarization(result);
        
        console.log('✅ Conversation summarized successfully');
        console.log(`💰 Cost: $${result.metadata.totalCost.toFixed(4)}`);
        console.log(`📊 Compression: ${result.metadata.compressionRatio}:1`);
        
      } catch (error) {
        console.error('❌ Summarization failed:', error.message);
      }
    }
  }

  /**
   * Apply summarization result to conversation history
   */
  async applySummarization(result) {
    // Backup original conversation
    const backupFile = path.join(this.dataDir, `chat-history-backup-${Date.now()}.json`);
    fs.copyFileSync(this.chatHistoryFile, backupFile);
    console.log(`📂 Backed up original to: ${backupFile}`);
    
    // Replace conversation with summary
    this.conversationLog = [{
      role: 'system',
      content: `[CONVERSATION SUMMARY - Original ${result.metadata.originalTokens} tokens compressed to ${result.metadata.summaryTokens} tokens]\n\n${result.summary}`,
      token_cnt: result.metadata.summaryTokens,
      timestamp: new Date().toISOString(),
      metadata: {
        type: 'summary',
        compressionRatio: result.metadata.compressionRatio,
        originalTokens: result.metadata.originalTokens,
        cost: result.metadata.totalCost
      }
    }];
    
    this.saveChatHistory();
  }

  /**
   * Force summarization (for testing or manual trigger)
   */
  async forceSummarize() {
    const conversationText = this.getConversationText();
    
    if (this.conversationLog.length === 0) {
      console.log('No conversation to summarize');
      return;
    }
    
    console.log('🔄 Starting forced summarization...');
    
    try {
      const result = await this.summarizer.summarizeConversation(conversationText);
      await this.applySummarization(result);
      
      console.log('✅ Forced summarization completed');
      return result;
      
    } catch (error) {
      console.error('❌ Forced summarization failed:', error.message);
      throw error;
    }
  }

  /**
   * Get conversation statistics
   */
  getStats() {
    const totalTokens = this.getTotalTokens();
    const messageCount = this.conversationLog.length;
    const averageTokensPerMessage = messageCount > 0 ? Math.round(totalTokens / messageCount) : 0;
    
    const summaryCount = this.conversationLog.filter(entry => 
      entry.metadata?.type === 'summary'
    ).length;
    
    return {
      messageCount,
      totalTokens,
      averageTokensPerMessage,
      summaryCount,
      needsSummarization: this.summarizer.shouldSummarize(this.getConversationText())
    };
  }

  /**
   * Estimate tokens (simple approximation)
   */
  estimateTokens(text) {
    return Math.ceil(text.length / 4);
  }
}

// Example usage in your MCP server
export class EnhancedMCPServer {
  constructor() {
    this.chatManager = new ChatHistoryManager();
  }

  /**
   * Handle incoming chat message (integrate with your existing server)
   */
  async handleChatMessage(userMessage) {
    // Add user message to history
    this.chatManager.addMessage('user', userMessage);
    
    // Generate AI response (your existing logic)
    const aiResponse = await this.generateAIResponse(userMessage);
    
    // Add AI response to history (this will trigger summarization if needed)
    this.chatManager.addMessage('assistant', aiResponse);
    
    return aiResponse;
  }

  /**
   * Get current conversation stats for UI display
   */
  getConversationStats() {
    return this.chatManager.getStats();
  }

  /**
   * Manual summarization endpoint (for /summarize command)
   */
  async triggerSummarization() {
    return await this.chatManager.forceSummarize();
  }

  /**
   * Your existing AI response generation
   */
  async generateAIResponse(message) {
    // Your existing moonshot/OpenAI API call logic here
    return "Sample AI response";
  }
}

// Example of adding summarization to your existing mcp-bridge-server.js
export function integrateSummarizationWithServer() {
  const enhancedServer = new EnhancedMCPServer();
  
  // Example integration points:
  return {
    // Add to your existing message handler
    handleMessage: async (message) => {
      return await enhancedServer.handleChatMessage(message);
    },
    
    // Add new /summarize command
    handleSummarizeCommand: async () => {
      return await enhancedServer.triggerSummarization();
    },
    
    // Get stats for C++ client
    getStats: () => {
      return enhancedServer.getConversationStats();
    }
  };
}

export default ChatHistoryManager;