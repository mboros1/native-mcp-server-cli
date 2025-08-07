import fs from 'fs';
import path from 'path';

/**
 * Tabular report generator for model configuration comparisons
 * Reads all config_*_responses.json files and generates comprehensive tables
 */

function findConfigFiles() {
    const dataDir = '.data';
    const files = fs.readdirSync(dataDir);
    return files
        .filter(file => file.startsWith('config_') && file.endsWith('_responses.json'))
        .map(file => path.join(dataDir, file));
}

function loadAllConfigurations() {
    const configFiles = findConfigFiles();
    console.log(`📊 Loading ${configFiles.length} configuration files...`);
    
    const configs = [];
    
    for (const file of configFiles) {
        try {
            const data = JSON.parse(fs.readFileSync(file, 'utf8'));
            if (!data.failed && data.performance) {
                configs.push({
                    file: path.basename(file),
                    ...data
                });
                console.log(`   ✓ ${data.configuration.name}`);
            } else {
                console.log(`   ❌ ${file} - failed or incomplete`);
            }
        } catch (error) {
            console.log(`   ❌ ${file} - parse error: ${error.message}`);
        }
    }
    
    return configs;
}

function generatePerformanceTable(configs) {
    console.log('\n📈 Generating performance comparison table...');
    
    // Sort by total cost for ranking
    const sorted = [...configs].sort((a, b) => a.performance.totalCost - b.performance.totalCost);
    
    let table = `# Model Configuration Performance Report

*Generated: ${new Date().toISOString()}*  
*Configurations tested: ${configs.length}*

## Performance Comparison Table

| Rank | Configuration | Fast Model | Smart Model | Total Cost | Time (s) | Compression | Cost/1k Tokens | Quality Score |
|------|---------------|------------|-------------|------------|----------|-------------|----------------|---------------|`;

    sorted.forEach((config, i) => {
        const perf = config.performance;
        const rank = i + 1;
        const costPer1k = (perf.costPerToken * 1000).toFixed(3);
        const qualityScore = calculateQualityScore(config);
        
        table += `
| ${rank} | ${config.configuration.name} | ${config.configuration.fast} | ${config.configuration.smart} | $${perf.totalCost.toFixed(4)} | ${(perf.totalTime/1000).toFixed(1)} | ${perf.compressionRatio}x | $${costPer1k} | ${qualityScore}/10 |`;
    });

    return table;
}

function calculateQualityScore(config) {
    // Simple quality scoring based on multiple factors
    let score = 5; // Base score
    
    // Adjust based on compression ratio (higher is better, up to a point)
    if (config.performance.compressionRatio > 10) score += 2;
    else if (config.performance.compressionRatio > 8) score += 1;
    else if (config.performance.compressionRatio < 6) score -= 1;
    
    // Adjust based on models used (reasoning models get bonus)
    if (config.configuration.fast.includes('o4') || config.configuration.smart.includes('o4')) score += 1;
    if (config.configuration.fast.includes('4.1') || config.configuration.smart.includes('4.1')) score += 0.5;
    
    // Adjust based on cost efficiency
    if (config.performance.totalCost < 0.05) score += 1;
    else if (config.performance.totalCost > 0.15) score -= 1;
    
    // Ensure score is within 1-10 range
    return Math.max(1, Math.min(10, Math.round(score * 10) / 10));
}

function generateDetailedAnalysis(configs) {
    let analysis = `

## Detailed Analysis

### Cost Analysis
`;

    const costSorted = [...configs].sort((a, b) => a.performance.totalCost - b.performance.totalCost);
    const fastest = [...configs].sort((a, b) => a.performance.totalTime - b.performance.totalTime)[0];
    const mostCompression = [...configs].sort((a, b) => b.performance.compressionRatio - a.performance.compressionRatio)[0];
    
    analysis += `
**Most Cost Effective:** ${costSorted[0].configuration.description}  
- Cost: $${costSorted[0].performance.totalCost.toFixed(4)}
- Models: ${costSorted[0].configuration.fast} → ${costSorted[0].configuration.smart}

**Most Expensive:** ${costSorted[costSorted.length-1].configuration.description}  
- Cost: $${costSorted[costSorted.length-1].performance.totalCost.toFixed(4)}
- Models: ${costSorted[costSorted.length-1].configuration.fast} → ${costSorted[costSorted.length-1].configuration.smart}

**Fastest Processing:** ${fastest.configuration.description}  
- Time: ${(fastest.performance.totalTime/1000).toFixed(1)}s
- Models: ${fastest.configuration.fast} → ${fastest.configuration.smart}

**Best Compression:** ${mostCompression.configuration.description}  
- Compression: ${mostCompression.performance.compressionRatio}x
- Models: ${mostCompression.configuration.fast} → ${mostCompression.configuration.smart}

### Model Performance Breakdown
`;

    // Group by fast model
    const byFastModel = {};
    configs.forEach(config => {
        const fastModel = config.configuration.fast;
        if (!byFastModel[fastModel]) byFastModel[fastModel] = [];
        byFastModel[fastModel].push(config);
    });

    Object.entries(byFastModel).forEach(([model, configs]) => {
        const avgCost = configs.reduce((sum, c) => sum + c.performance.totalCost, 0) / configs.length;
        const avgTime = configs.reduce((sum, c) => sum + c.performance.totalTime, 0) / configs.length;
        const avgCompression = configs.reduce((sum, c) => sum + c.performance.compressionRatio, 0) / configs.length;
        
        analysis += `
#### ${model} as Fast Model (${configs.length} configs)
- Average Cost: $${avgCost.toFixed(4)}
- Average Time: ${(avgTime/1000).toFixed(1)}s  
- Average Compression: ${avgCompression.toFixed(1)}x
- Smart models tested: ${[...new Set(configs.map(c => c.configuration.smart))].join(', ')}
`;
    });

    return analysis;
}

function generateSummaryTables(configs) {
    let tables = `

## Summary Tables

### By Fast Model Performance
| Fast Model | Configs | Avg Cost | Avg Time | Avg Compression | Best Smart Model |
|------------|---------|----------|----------|-----------------|------------------|`;

    const byFast = {};
    configs.forEach(config => {
        const fast = config.configuration.fast;
        if (!byFast[fast]) byFast[fast] = [];
        byFast[fast].push(config);
    });

    Object.entries(byFast).forEach(([fast, configs]) => {
        const avgCost = configs.reduce((sum, c) => sum + c.performance.totalCost, 0) / configs.length;
        const avgTime = configs.reduce((sum, c) => sum + c.performance.totalTime, 0) / configs.length;
        const avgCompression = configs.reduce((sum, c) => sum + c.performance.compressionRatio, 0) / configs.length;
        const bestConfig = configs.sort((a, b) => a.performance.totalCost - b.performance.totalCost)[0];
        
        tables += `
| ${fast} | ${configs.length} | $${avgCost.toFixed(4)} | ${(avgTime/1000).toFixed(1)}s | ${avgCompression.toFixed(1)}x | ${bestConfig.configuration.smart} |`;
    });

    tables += `

### Cost Tier Analysis  
| Tier | Cost Range | Configurations | Best Performer |
|------|------------|----------------|----------------|`;

    const tiers = [
        { name: 'Ultra Budget', min: 0, max: 0.05, configs: [] },
        { name: 'Budget', min: 0.05, max: 0.08, configs: [] },
        { name: 'Standard', min: 0.08, max: 0.12, configs: [] },
        { name: 'Premium', min: 0.12, max: 0.20, configs: [] },
        { name: 'Luxury', min: 0.20, max: 1.00, configs: [] }
    ];

    configs.forEach(config => {
        const cost = config.performance.totalCost;
        const tier = tiers.find(t => cost >= t.min && cost < t.max);
        if (tier) tier.configs.push(config);
    });

    tiers.forEach(tier => {
        if (tier.configs.length > 0) {
            const best = tier.configs.sort((a, b) => a.performance.totalCost - b.performance.totalCost)[0];
            tables += `
| ${tier.name} | $${tier.min.toFixed(2)}-$${tier.max.toFixed(2)} | ${tier.configs.length} | ${best.configuration.description} |`;
        }
    });

    return tables;
}

function generateRecommendations(configs) {
    let recommendations = `

## Recommendations

### Best Overall Configurations

`;

    // Find best in different categories
    const bestCost = [...configs].sort((a, b) => a.performance.totalCost - b.performance.totalCost)[0];
    const bestSpeed = [...configs].sort((a, b) => a.performance.totalTime - b.performance.totalTime)[0];
    const bestCompression = [...configs].sort((a, b) => b.performance.compressionRatio - a.performance.compressionRatio)[0];
    const bestQuality = [...configs].sort((a, b) => calculateQualityScore(b) - calculateQualityScore(a))[0];

    recommendations += `
🏆 **Best Cost Efficiency:** ${bestCost.configuration.description}  
💰 Cost: $${bestCost.performance.totalCost.toFixed(4)} | ⚡ Time: ${(bestCost.performance.totalTime/1000).toFixed(1)}s | 🗜️ Compression: ${bestCost.performance.compressionRatio}x

⚡ **Fastest Processing:** ${bestSpeed.configuration.description}  
💰 Cost: $${bestSpeed.performance.totalCost.toFixed(4)} | ⚡ Time: ${(bestSpeed.performance.totalTime/1000).toFixed(1)}s | 🗜️ Compression: ${bestSpeed.performance.compressionRatio}x

🗜️ **Best Compression:** ${bestCompression.configuration.description}  
💰 Cost: $${bestCompression.performance.totalCost.toFixed(4)} | ⚡ Time: ${(bestCompression.performance.totalTime/1000).toFixed(1)}s | 🗜️ Compression: ${bestCompression.performance.compressionRatio}x

🎯 **Highest Quality Score:** ${bestQuality.configuration.description}  
💰 Cost: $${bestQuality.performance.totalCost.toFixed(4)} | ⚡ Time: ${(bestQuality.performance.totalTime/1000).toFixed(1)}s | 🗜️ Compression: ${bestQuality.performance.compressionRatio}x | 🌟 Score: ${calculateQualityScore(bestQuality)}/10

### Use Case Recommendations

📱 **For Production/High-Volume:** ${bestCost.configuration.description}  
Reasoning: Lowest cost per operation while maintaining quality

🚀 **For Real-Time Applications:** ${bestSpeed.configuration.description}  
Reasoning: Fastest processing time for immediate results  

📚 **For Research/Analysis:** ${bestCompression.configuration.description}  
Reasoning: Maximum information density preservation

💎 **For Premium Applications:** ${bestQuality.configuration.description}  
Reasoning: Highest overall quality score considering all factors
`;

    return recommendations;
}

function generateFullReport() {
    console.log('🚀 Generating comprehensive tabular report...');
    
    const configs = loadAllConfigurations();
    
    if (configs.length === 0) {
        console.log('❌ No valid configuration files found!');
        return;
    }
    
    let report = generatePerformanceTable(configs);
    report += generateDetailedAnalysis(configs);
    report += generateSummaryTables(configs);  
    report += generateRecommendations(configs);
    
    report += `

---

## Raw Data Summary

**Configuration Files Processed:**
${configs.map(c => `- ${c.file} (${c.configuration.description})`).join('\n')}

**Test Parameters:**
- Input Tokens: ${configs[0]?.metadata?.inputStats?.totalTokens?.toLocaleString() || 'N/A'}
- Input Messages: ${configs[0]?.metadata?.inputStats?.messageCount || 'N/A'}
- Target Summary Tokens: ~2000

*This report was automatically generated from individual configuration test results.*
*Each configuration's detailed results and raw model outputs are available in separate JSON and markdown files.*
`;

    const outputFile = '.data/MODEL_PERFORMANCE_REPORT.md';
    fs.writeFileSync(outputFile, report);
    
    console.log(`✅ Comprehensive report generated: ${outputFile}`);
    console.log(`📊 Analyzed ${configs.length} configurations`);
    
    return report;
}

// Export for use as module or run directly
export default generateFullReport;

if (import.meta.url === `file://${process.argv[1]}`) {
    generateFullReport();
}