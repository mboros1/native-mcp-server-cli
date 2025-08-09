import fs from 'fs';
import ConversationSummarizer from './summarization.js';
import jsonToRawMarkdown from './json-to-raw-markdown.js';

/**
 * Quick configuration testing with just a few key configs
 */

const QUICK_TEST_CONFIGURATIONS = [
    {
        name: 'baseline_4o',
        fast: 'gpt-4o-mini',
        smart: 'gpt-4o',
        description: 'Baseline: 4o-mini → 4o'
    },
    {
        name: 'nano_to_o4mini',
        fast: 'gpt-4.1-nano',
        smart: 'o4-mini', 
        description: 'Budget to reasoning: 4.1-nano → o4-mini'
    },
    {
        name: 'all_o4mini',
        fast: 'o4-mini',
        smart: 'o4-mini',
        description: 'All reasoning: o4-mini → o4-mini'
    }
];

class QuickTester {
    constructor() {
        this.results = [];
        this.conversationText = null;
        this.inputStats = null;
    }

    async loadConversation() {
        console.log('📊 Loading test conversation...');
        
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
        this.conversationText = messages.map(msg => `${msg.role}: ${msg.content}`).join('\n\n');
        
        this.inputStats = {
            messageCount: messages.length,
            totalTokens,
            averageTokensPerMessage: Math.round(totalTokens / messages.length)
        };
        
        console.log(`   ✓ ${messages.length} messages, ${totalTokens.toLocaleString()} tokens loaded`);
    }

    async testConfiguration(config) {
        console.log(`\n🧪 Testing: ${config.description}`);
        
        class QuickSummarizer extends ConversationSummarizer {
            constructor(options, configName) {
                super(options);
                this.configName = configName;
                this.capturedResponses = [];
                this.callIndex = 0;
            }

            async makeOpenAICall(model, messages, maxTokens = null, temperature = 0.7) {
                const callId = `${this.configName}_call_${++this.callIndex}`;
                console.log(`   📡 ${callId}: ${model}`);
                
                const startTime = Date.now();
                const result = await super.makeOpenAICall(model, messages, maxTokens, temperature);
                const duration = Date.now() - startTime;
                
                this.capturedResponses.push({
                    callId,
                    timestamp: new Date().toISOString(),
                    duration,
                    model,
                    request: {
                        messages: messages,
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
                        focus: this.callIndex <= 3 ? ['comprehensive', 'technical', 'strategic'][this.callIndex - 1] : 'quality_enhancement',
                        configuration: this.configName
                    }
                });
                
                return result;
            }
        }
        
        const summarizer = new QuickSummarizer({
            cheapModel: config.fast,
            qualityModel: config.smart,
            triggerTokens: 10000,
            targetSummaryTokens: 1500,  // Smaller target for speed
            logger: { 
                info: () => {}, 
                warn: console.warn, 
                error: console.error,
                debug: () => {}
            }
        }, config.name);
        
        try {
            const startTime = Date.now();
            const parallelSummaries = await summarizer.generateParallelSummaries(this.conversationText, 3);
            const finalSummary = await summarizer.selectAndEnhanceSummary(this.conversationText, parallelSummaries);
            const totalTime = Date.now() - startTime;
            
            const totalCost = summarizer.capturedResponses.reduce((sum, r) => sum + (r.response.cost || 0), 0);
            
            const result = {
                configuration: config,
                performance: {
                    totalTime,
                    totalCost,
                    compressionRatio: Math.round(this.inputStats.totalTokens / finalSummary.tokens * 10) / 10,
                    costPerToken: totalCost / this.inputStats.totalTokens,
                    apiCalls: summarizer.capturedResponses.length
                },
                modelResponses: summarizer.capturedResponses,
                results: {
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
            
            console.log(`   ✅ Cost: $${totalCost.toFixed(4)}, Time: ${(totalTime/1000).toFixed(1)}s, Compression: ${result.performance.compressionRatio}x`);
            
            // Save individual result
            const filename = `.data/config_${config.name}_responses.json`;
            const dataToSave = {
                metadata: {
                    timestamp: new Date().toISOString(),
                    configuration: config,
                    inputStats: this.inputStats
                },
                ...result
            };
            fs.writeFileSync(filename, JSON.stringify(dataToSave, null, 2));
            console.log(`   💾 Saved: ${filename}`);
            
            return result;
            
        } catch (error) {
            console.error(`   ❌ Configuration ${config.name} failed:`, error.message);
            return {
                configuration: config,
                error: error.message,
                failed: true
            };
        }
    }

    async runQuickTests() {
        console.log(`🚀 Running ${QUICK_TEST_CONFIGURATIONS.length} quick configurations\n`);
        
        await this.loadConversation();
        
        for (const config of QUICK_TEST_CONFIGURATIONS) {
            const result = await this.testConfiguration(config);
            this.results.push(result);
        }
        
        console.log('\n📊 Quick test complete!');
        console.log('Run `node generate-tabular-report.js` to see comparison report.');
    }
}

// Run quick tests
const tester = new QuickTester();
tester.runQuickTests();