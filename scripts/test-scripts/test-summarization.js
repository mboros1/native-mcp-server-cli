import ConversationSummarizer from './summarization.js';
import fs from 'fs';

/**
 * Test script for the ConversationSummarizer module
 * 
 * Usage:
 * 1. Add OPENAI_API_KEY to your .env file
 * 2. Run: node test-summarization.js
 */

async function testSummarization() {
  console.log('🚀 Testing Conversation Summarization Module');
  
  // Initialize summarizer with custom options
  const summarizer = new ConversationSummarizer({
    triggerTokens: 1000,  // Lower threshold for testing
    targetSummaryTokens: 500,  // Smaller summaries for testing
    cheapModel: 'gpt-4o-mini',  // Use available model for testing
    qualityModel: 'gpt-4o-mini', // Use same model for testing
    logger: console
  });

  // Sample long conversation for testing
  const testConversation = `
User: I need help implementing a conversation token size tracking system for my native MCP client. The client is written in C++ and uses FTXUI for the terminal UI, while I have a Node.js server that handles the API calls.