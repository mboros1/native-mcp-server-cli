import fs from 'fs';
import ConversationSummarizer from './summarization.js';

async function captureRawResponses() {
    console.log('🔬 Capturing Raw Model Responses for Analysis\n');
    
    try {
        // Load conversation
        const conversationData = fs.readFileSync('.data/working.chat-history.json', 'utf8');
        const lines = conversationData.trim().split('\n');
        
        const messages = lines
            .filter(line => line.trim())
            .map(line => {
                const entry = JSON.parse(line);
                return {
                    role: entry.role,
                    content: entry.content,
                    tokens: entry.token_cnt,
                    timestamp: entry.timestamp
                };
            });
        
        const totalTokens = messages.reduce((sum, msg) => sum + msg.tokens, 0);
        const conversationText = messages.map(msg => `${msg.role}: ${msg.content}`).join('\n\n');
        
        console.log(`📊 Input: ${messages.length} messages, ${totalTokens.toLocaleString()} tokens\n`);
        
        // Enhanced summarizer that captures everything
        class ResponseCaptureSummarizer extends ConversationSummarizer {
            constructor(options) {
                super(options);
                this.capturedResponses = [];
                this.callIndex = 0;
            }

            async makeOpenAICall(model, messages, maxTokens = null, temperature = 0.7) {
                const callId = `call_${++this.callIndex}`;
                console.log(`📡 API Call ${callId}: ${model}`);
                
                const startTime = Date.now();
                const result = await super.makeOpenAICall(model, messages, maxTokens, temperature);
                const duration = Date.now() - startTime;
                
                // Capture full request/response
                this.capturedResponses.push({
                    callId,
                    timestamp: new Date().toISOString(),
                    duration,
                    model,
                    request: {
                        messages: messages.map(m => ({
                            role: m.role,
                            content: m.content.length > 500 ? m.content.substring(0, 500) + '...[truncated]' : m.content
                        })),
                        model,
                        temperature,
                        max_tokens: maxTokens
                    },
                    response: {
                        content: result.content,
                        usage: result.usage,
                        cost: result.cost,
                        finishReason: result.finish_reason || 'stop'
                    },
                    metadata: {
                        phase: this.callIndex <= 3 ? 'parallel_generation' : 'final_selection',
                        focus: this.callIndex <= 3 ? ['comprehensive', 'technical', 'strategic'][this.callIndex - 1] : 'quality_enhancement'
                    }
                });
                
                return result;
            }
        }
        
        const summarizer = new ResponseCaptureSummarizer({
            cheapModel: 'gpt-4o-mini',
            qualityModel: 'gpt-4o',
            triggerTokens: 10000,
            targetSummaryTokens: 2000,
            logger: { 
                info: () => {}, 
                warn: console.warn, 
                error: console.error,
                debug: () => {}
            }
        });
        
        // Run summarization
        console.log('🔄 Starting capture...\n');
        const startTime = Date.now();
        const parallelSummaries = await summarizer.generateParallelSummaries(conversationText, 3);
        const finalSummary = await summarizer.selectAndEnhanceSummary(conversationText, parallelSummaries);
        const totalTime = Date.now() - startTime;
        
        // Build complete dataset
        const completeDataset = {
            metadata: {
                captureTimestamp: new Date().toISOString(),
                totalProcessingTime: totalTime,
                totalCost: summarizer.capturedResponses.reduce((sum, r) => sum + r.response.cost, 0),
                totalTokensProcessed: summarizer.capturedResponses.reduce((sum, r) => 
                    sum + r.response.usage.prompt_tokens + r.response.usage.completion_tokens, 0
                )
            },
            input: {
                conversationStats: {
                    messageCount: messages.length,
                    totalTokens,
                    averageTokensPerMessage: Math.round(totalTokens / messages.length)
                },
                conversationText: conversationText.substring(0, 2000) + '...[truncated]',
                sampleMessages: messages.slice(0, 3)
            },
            modelResponses: summarizer.capturedResponses,
            processedResults: {
                parallelSummaries: parallelSummaries.map((s, i) => ({
                    index: i + 1,
                    focus: ['comprehensive', 'technical', 'strategic'][i],
                    content: s.summary,
                    tokens: s.tokens,
                    cost: s.cost
                })),
                finalSummary: {
                    content: finalSummary.summary,
                    tokens: finalSummary.tokens,
                    cost: finalSummary.cost,
                    model: finalSummary.model
                }
            }
        };
        
        // Save dataset
        const outputPath = '.data/raw-model-responses.json';
        fs.writeFileSync(outputPath, JSON.stringify(completeDataset, null, 2));
        
        console.log(`\n✅ Complete dataset saved to: ${outputPath}`);
        console.log(`📊 Captured ${summarizer.capturedResponses.length} API calls`);
        console.log(`💰 Total cost: $${completeDataset.metadata.totalCost.toFixed(4)}`);
        
        return completeDataset;
        
    } catch (error) {
        console.error('❌ Capture failed:', error.message);
        throw error;
    }
}

// Run capture
captureRawResponses();