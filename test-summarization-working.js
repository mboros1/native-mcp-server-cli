import fs from 'fs';
import ConversationSummarizer from './summarization.js';

async function testSummarization() {
    console.log('🚀 Testing summarization on working conversation...\n');
    
    try {
        // Load the working conversation
        const conversationData = fs.readFileSync('.data/working.chat-history.json', 'utf8');
        const lines = conversationData.trim().split('\n');
        
        // Parse NDJSON format
        const messages = lines
            .filter(line => line.trim())
            .map(line => {
                const entry = JSON.parse(line);
                return {
                    role: entry.role,
                    content: entry.content
                };
            });
        
        console.log(`📊 Loaded ${messages.length} messages`);
        
        // Calculate total tokens
        const totalTokens = lines
            .filter(line => line.trim())
            .map(line => JSON.parse(line).token_cnt)
            .reduce((sum, tokens) => sum + tokens, 0);
        
        console.log(`📏 Total tokens: ${totalTokens.toLocaleString()}`);
        console.log(`📖 Conversation preview: "${messages[0].content.substring(0, 100)}..."\n`);
        
        // Convert messages to conversation text
        const conversationText = messages.map(msg => 
            `${msg.role}: ${msg.content}`
        ).join('\n\n');
        
        // Initialize summarizer
        const summarizer = new ConversationSummarizer({
            cheapModel: 'gpt-4o-mini',     // Use available models
            qualityModel: 'gpt-4o',        // Use available models
            triggerTokens: 10000,          // Lower threshold for testing
            targetSummaryTokens: 2000,     // Smaller target for testing
            logger: console
        });
        
        console.log('🔄 Starting ensemble summarization...\n');
        
        // Run the summarization
        const startTime = Date.now();
        const result = await summarizer.summarizeConversation(conversationText);
        const duration = Date.now() - startTime;
        
        console.log('\n✅ Summarization completed!\n');
        console.log('📋 SUMMARY:');
        console.log('='.repeat(80));
        console.log(result.summary);
        console.log('='.repeat(80));
        
        console.log('\n📊 STATISTICS:');
        console.log(`⏱️  Duration: ${(duration / 1000).toFixed(1)}s`);
        console.log(`💰 Total Cost: $${result.metadata.totalCost.toFixed(4)}`);
        console.log(`🔀 Original Tokens: ${result.metadata.originalTokens.toLocaleString()}`);
        console.log(`📝 Summary Tokens: ${result.metadata.summaryTokens.toLocaleString()}`);
        console.log(`📉 Compression: ${result.metadata.compressionRatio.toFixed(1)}x`);
        
        console.log('\n🔧 PROCESS DETAILS:');
        console.log(`📤 Parallel summaries: ${result.metadata.candidateSummaries}`);
        console.log(`💵 Cost breakdown: Parallel $${result.metadata.costBreakdown.parallelPhase.toFixed(4)}, Final $${result.metadata.costBreakdown.finalPhase.toFixed(4)}`);
        
        // Save result for inspection
        const outputPath = '.data/summarization-result.json';
        fs.writeFileSync(outputPath, JSON.stringify(result, null, 2));
        console.log(`\n💾 Full results saved to: ${outputPath}`);
        
    } catch (error) {
        console.error('❌ Error during summarization:', error.message);
        if (error.response) {
            console.error('📡 API Response:', error.response.status, error.response.data);
        }
        console.error('🔍 Stack trace:', error.stack);
    }
}

// Run the test
testSummarization();