// test-o3.js
// Minimal test script for o3 responses API
require('dotenv').config();
const axios = require('axios');
const fs = require('fs/promises');
const path = require('path');

const CHAT_HISTORY_FILE = path.join(__dirname, '.data', 'chat-history.json');
const LATEST_USER_TEXT = 'can you summarize our current conversation, focusing on clarity of details and preservation of context?';

(async () => {
  if (!process.env.OPENAI_API_KEY) {
    console.error('Missing OPENAI_API_KEY in .env');
    process.exit(1);
  }

  // Load chat history (JSONL format)
  let chatHistory = [];
  try {
    const data = await fs.readFile(CHAT_HISTORY_FILE, 'utf8');
    chatHistory = data.trim().split('\n').map(line => JSON.parse(line));
  } catch (error) {
    console.error('Could not load chat history:', error.message);
    process.exit(1);
  }

  // Convert chat history to simple text format for o3
  const conversationText = chatHistory
    .map(msg => `${msg.role}: ${msg.content}`)
    .join('\n\n');

  const requestBody = {
    model: 'o3',
    input: conversationText + `\n\nuser: ${LATEST_USER_TEXT}`,
    text: { format: { type: 'text' } },
    reasoning: { effort: 'medium' },
    max_output_tokens: 1024
  };

  console.log('Request body:', JSON.stringify(requestBody, null, 2));

  try {
    const { data } = await axios.post(
      'https://api.openai.com/v1/responses',
      requestBody,
      { 
        headers: { 
          'Authorization': `Bearer ${process.env.OPENAI_API_KEY}`,
          'Content-Type': 'application/json'
        } 
      }
    );

    console.log('\n=== RESPONSE ===');
    console.log('Full response:', JSON.stringify(data, null, 2));
    
    // Try to extract the text content
    const responseText = data.output?.[0]?.content?.[0]?.text || 'No response content found';
    console.log('\n=== EXTRACTED TEXT ===');
    console.log(responseText);

  } catch (error) {
    console.error('Error:', error.message);
    if (error.response) {
      console.error('Status:', error.response.status);
      console.error('Data:', error.response.data);
    }
  }
})();