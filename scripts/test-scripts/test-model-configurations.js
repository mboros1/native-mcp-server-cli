import fs from 'fs';
import ConversationSummarizer from './summarization.js';
import jsonToRawMarkdown from './json-to-raw-markdown.js';

/**
 * Multi-configuration testing framework for ensemble summarization
 * Tests different FAST/SMART model combinations
 */

const TEST_CONFIGURATIONS = [
    {
        name: 'baseline_4o',
        fast: 'gpt-4o-mini',
        smart: 'gpt-4o',
        description: 'Baseline: 4o-mini → 4o',
        expectedCost: 'medium'
    },
    {
        name: 'gpt41_mini_to_41',
        fast: 'gpt-4.1-mini',
        smart: 'gpt-4.1',
        description: 'Premium: 4.1-mini → 4.1',
        expectedCost: 'high'
    },
    {
        name: 'gpt41_nano_to_41',
        fast: 'gpt-4.1-nano',
        smart: 'gpt-4.1',
        description: 'Ultra-cheap parallel: 4.1-nano → 4.1',
        expectedCost: 'medium-high'
    },
    {
        name: 'gpt41_to_4omini',
        fast: 'gpt-4.1',
        smart: 'gpt-4o-mini',
        description: 'Inverted premium: 4.1 → 4o-mini',
        expectedCost: 'medium-high'
    },
    {
        name: 'all_41',
        fast: 'gpt-4.1',
        smart: 'gpt-4.1',
        description: 'All premium: 4.1 → 4.1',
        expectedCost: 'highest'
    },
    {
        name: 'nano_to_4omini',
        fast: 'gpt-4.1-nano',
        smart: 'gpt-4o-mini',
        description: 'Budget: 4.1-nano → 4o-mini',
        expectedCost: 'low'
    },
    {
        name: 'o4mini_to_4o',
        fast: 'o4-mini',
        smart: 'gpt-4o',
        description: 'Reasoning fast: o4-mini → 4o',
        expectedCost: 'medium-high'
    },
    {
        name: 'o4mini_to_41',
        fast: 'o4-mini',
        smart: 'gpt-4.1',
        description: 'Reasoning to premium: o4-mini → 4.1',
        expectedCost: 'high'
    },
    {
        name: 'nano_to_o4mini',
        fast: 'gpt-4.1-nano',
        smart: 'o4-mini',
        description: 'Budget to reasoning: 4.1-nano → o4-mini',
        expectedCost: 'medium'
    },
    {
        name: 'all_o4mini',
        fast: 'o4-mini',
        smart: 'o4-mini',
        description: 'All reasoning: o4-mini → o4-mini',
        expectedCost: 'high'
    }
];

class ConfigurationTester {
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
            averageTokensPerMessage: Math.round(totalTokens / messages.length),
            conversationLength: this.conversationText.length
        };
        
        console.log(`   ✓ ${messages.length} messages, ${totalTokens.toLocaleString()} tokens loaded`);
    }

    async testConfiguration(config) {
        console.log(`\n🧪 Testing Configuration: ${config.description}`);
        console.log(`   FAST: ${config.fast} → SMART: ${config.smart}\n`);
        
        // Enhanced summarizer for this config
        class ConfigSummarizer extends ConversationSummarizer {
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
        
        const summarizer = new ConfigSummarizer({
            cheapModel: config.fast,
            qualityModel: config.smart,
            triggerTokens: 10000,
            targetSummaryTokens: 2000,
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
            const totalTokens = summarizer.capturedResponses.reduce((sum, r) => 
                sum + (r.response.usage?.prompt_tokens || 0) + (r.response.usage?.completion_tokens || 0), 0);
            
            const result = {
                configuration: config,
                performance: {
                    totalTime,
                    totalCost,
                    totalTokens,
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

    async runAllConfigurations() {
        console.log(`🚀 Running ${TEST_CONFIGURATIONS.length} model configurations\n`);
        
        await this.loadConversation();
        
        for (const config of TEST_CONFIGURATIONS) {
            try {
                const result = await this.testConfiguration(config);
                this.results.push(result);
                
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
                
                // Generate raw markdown for this config
                jsonToRawMarkdown(filename, filename.replace('.json', '_RAW.md'));
                
            } catch (error) {
                console.error(`❌ Config ${config.name} failed completely:`, error);
            }
        }
        
        // Generate comparison report
        await this.generateComparisonReport();
    }

    async generateComparisonReport() {
        console.log('\n📊 Generating comparison report...');
        
        const validResults = this.results.filter(r => !r.failed);
        
        let report = `# Model Configuration Comparison Report

*Generated: ${new Date().toISOString()}*  
*Test Dataset: ${this.inputStats.totalTokens.toLocaleString()} tokens, ${this.inputStats.messageCount} messages*

---

## Configuration Performance Summary

| Configuration | Fast Model | Smart Model | Cost | Time (s) | Compression | Cost/Token |
|---------------|------------|-------------|------|----------|-------------|------------|`;

        validResults.forEach(result => {
            const perf = result.performance;
            report += `
| ${result.configuration.name} | ${result.configuration.fast} | ${result.configuration.smart} | $${perf.totalCost.toFixed(4)} | ${(perf.totalTime/1000).toFixed(1)} | ${perf.compressionRatio}x | $${(perf.costPerToken * 1000).toFixed(3)}/1k |`;
        });

        report += `

---

## Detailed Results

`;

        validResults.forEach(result => {
            const config = result.configuration;
            const perf = result.performance;
            
            report += `### ${config.description}

**Configuration:**
- FAST Model: ${config.fast}
- SMART Model: ${config.smart}

**Performance:**
- Total Cost: $${perf.totalCost.toFixed(4)}
- Processing Time: ${(perf.totalTime/1000).toFixed(1)}s
- Compression Ratio: ${perf.compressionRatio}:1
- Cost per 1k tokens: $${(perf.costPerToken * 1000).toFixed(3)}

**Parallel Summaries Generated:**
${result.results.parallelSummaries.map(s => 
    `- **${s.focus}:** ${s.tokens} tokens, $${s.cost.toFixed(4)}`
).join('\n')}

**Final Summary:**
- Tokens: ${result.results.finalSummary.tokens}
- Cost: $${result.results.finalSummary.cost.toFixed(4)}
- Model: ${result.results.finalSummary.model}

---

`;
        });

        // Best performers analysis
        const sortedByCost = [...validResults].sort((a, b) => a.performance.totalCost - b.performance.totalCost);
        const sortedByTime = [...validResults].sort((a, b) => a.performance.totalTime - b.performance.totalTime);
        const sortedByCompression = [...validResults].sort((a, b) => b.performance.compressionRatio - a.performance.compressionRatio);

        report += `## Best Performers

**Most Cost Effective:** ${sortedByCost[0].configuration.description} ($${sortedByCost[0].performance.totalCost.toFixed(4)})  
**Fastest Processing:** ${sortedByTime[0].configuration.description} (${(sortedByTime[0].performance.totalTime/1000).toFixed(1)}s)  
**Best Compression:** ${sortedByCompression[0].configuration.description} (${sortedByCompression[0].performance.compressionRatio}x)

---

*Individual configuration data saved to .data/config_*_responses.json*  
*Raw markdown reports available as .data/config_*_RAW.md*
`;

        fs.writeFileSync('.data/MODEL_CONFIGURATION_COMPARISON.md', report);
        
        console.log('✅ Comparison report saved: .data/MODEL_CONFIGURATION_COMPARISON.md');
        console.log(`📊 Tested ${validResults.length}/${TEST_CONFIGURATIONS.length} configurations successfully`);
    }
}

// Run the tests
const tester = new ConfigurationTester();
tester.runAllConfigurations();