import { DynamicChatProcedures } from './server/lib/jsonrpc/procedures-dynamic.js';
import { ApiManager } from './server/lib/jsonrpc/ApiManager.js';
import { ChatHistory } from './server/lib/jsonrpc/ChatHistory.js';
import { initializeToolRouter } from './server/tools/toolRouter-dynamic.js';
import { log } from './server/lib/logger.js';
import path from 'path';
import { fileURLToPath } from 'url';
import { dirname } from 'path';

const __filename = fileURLToPath(import.meta.url);
const __dirname = dirname(__filename);

async function testAgent() {
    console.log('Testing agent with simple file discovery...');
    
    // Initialize components
    const apiManager = new ApiManager();
    const dataDir = path.join(__dirname, '.data');
    const chatHistory = new ChatHistory(dataDir);
    
    // Initialize dynamic tools
    await initializeToolRouter();
    
    // Create chat procedures with agent support
    const chat = new DynamicChatProcedures(apiManager, chatHistory);
    
    // Test message
    const testMessage = "List the files in the current directory";
    
    try {
        console.log(`\nSending: "${testMessage}"`);
        const result = await chat.send({
            content: testMessage,
            model: 'kimi'
        });
        
        console.log('\nSuccess! Response:');
        console.log(result.reply);
    } catch (error) {
        console.error('\nError:', error.message);
        if (error.response) {
            console.error('Response status:', error.response.status);
            console.error('Response data:', error.response.data);
        }
    }
    
    process.exit(0);
}

// Set agent mode
process.env.USE_AGENT_MODE = 'true';
process.env.DEBUG_AGENTS = 'true';

testAgent().catch(console.error);