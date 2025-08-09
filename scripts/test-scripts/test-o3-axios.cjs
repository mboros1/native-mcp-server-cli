// test-o3-axios.cjs
// Test using the same axios instance as main server
require('dotenv').config();
const axios = require('axios');

// Create axios instance same as main server
const openai = axios.create({
  baseURL: 'https://api.openai.com',
  timeout: 5 * 60_000,
  headers: {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${process.env.OPENAI_API_KEY}`,
  },
});

(async () => {
  const requestBody = {
    model: 'o3',
    input: 'user: What is 2+2?',
    text: { format: { type: 'text' } },
    reasoning: { effort: 'medium' },
    max_output_tokens: 100
  };

  console.log('Request body:', JSON.stringify(requestBody, null, 2));

  try {
    const response = await openai.post('/v1/responses', requestBody);
    console.log('Response status:', response.status);
    console.log('Response data exists:', !!response.data);
    console.log('Response data type:', typeof response.data);
    if (response.data) {
      console.log('Response keys:', Object.keys(response.data));
      console.log('Has output?:', !!response.data.output);
      // Check text field at root level
      if (response.data.text) {
        console.log('Text field:', response.data.text);
      }
      
      // Also check output array
      if (response.data.output) {
        console.log('Output length:', response.data.output.length);
        response.data.output.forEach((item, idx) => {
          console.log(`Output[${idx}]:`, JSON.stringify(item, null, 2).substring(0, 500));
        });
      }
    }
  } catch (error) {
    console.error('Error:', error.message);
    if (error.response) {
      console.error('Status:', error.response.status);
      console.error('Data:', error.response.data);
    }
  }
})();