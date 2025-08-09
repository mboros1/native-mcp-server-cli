import fs from 'fs';

/**
 * Quick and dirty JSON to Markdown converter for raw model responses
 * Converts the captured API responses into readable markdown format
 */

function jsonToMarkdown(inputFile, outputFile) {
    console.log(`🔄 Converting ${inputFile} to ${outputFile}...`);
    
    try {
        // Load the JSON data
        const data = JSON.parse(fs.readFileSync(inputFile, 'utf8'));
        
        let markdown = `# Raw Model Responses Analysis

*Generated from: ${inputFile}*  
*Timestamp: ${data.metadata.captureTimestamp}*

---

## Overview

**Processing Stats:**
- Total Processing Time: ${(data.metadata.totalProcessingTime / 1000).toFixed(1)}s
- Total Cost: $${data.metadata.totalCost?.toFixed(4) || 'N/A'}
- Total Tokens Processed: ${data.metadata.totalTokensProcessed?.toLocaleString() || 'N/A'}
- API Calls Made: ${data.modelResponses.length}

**Input Conversation:**
- Messages: ${data.input.conversationStats.messageCount}
- Total Tokens: ${data.input.conversationStats.totalTokens.toLocaleString()}
- Average per Message: ${data.input.conversationStats.averageTokensPerMessage}

---

## Sample Input Messages

`;

        // Add sample messages
        data.input.sampleMessages.forEach((msg, i) => {
            markdown += `### Message ${i + 1} (${msg.role})
**Tokens:** ${msg.tokens}  
**Timestamp:** ${msg.timestamp}

\`\`\`
${msg.content.substring(0, 300)}${msg.content.length > 300 ? '...' : ''}
\`\`\`

`;
        });

        markdown += `---

## Model API Calls

`;

        // Process each API call
        data.modelResponses.forEach((call, i) => {
            const phase = call.metadata.phase.replace('_', ' ').toUpperCase();
            const focus = call.metadata.focus.replace('_', ' ');
            
            markdown += `### Call ${call.callId}: ${call.model}

**Phase:** ${phase}  
**Focus:** ${focus}  
**Duration:** ${call.duration}ms  
**Timestamp:** ${call.timestamp}

#### Request
- **Model:** ${call.request.model}
- **Temperature:** ${call.request.temperature}
- **Max Tokens:** ${call.request.max_tokens || 'default'}
- **Messages:** ${call.request.messages.length}

**System/User Messages:**
`;

            // Show request messages (truncated)
            call.request.messages.forEach((msg, j) => {
                if (j < 2) { // Only show first 2 messages to keep readable
                    markdown += `
**${msg.role.toUpperCase()}:**
\`\`\`
${msg.content}
\`\`\`
`;
                }
            });

            if (call.request.messages.length > 2) {
                markdown += `
*[${call.request.messages.length - 2} additional messages truncated for brevity]*
`;
            }

            markdown += `
#### Response
- **Tokens:** ${call.response.usage?.completion_tokens || 'N/A'} out / ${call.response.usage?.prompt_tokens || 'N/A'} in
- **Total:** ${call.response.usage?.total_tokens || 'N/A'} tokens
- **Cost:** $${call.response.cost?.toFixed(4) || 'N/A'}
- **Finish Reason:** ${call.response.finishReason}

**Generated Content:**
\`\`\`markdown
${call.response.content.substring(0, 1000)}${call.response.content.length > 1000 ? '\n\n[...truncated for brevity...]' : ''}
\`\`\`

---

`;
        });

        markdown += `## Processed Results Summary

### Parallel Summaries Generated

`;

        // Show parallel summaries
        data.processedResults.parallelSummaries.forEach((summary) => {
            markdown += `#### Summary ${summary.index}: ${summary.focus.toUpperCase()}
- **Tokens:** ${summary.tokens}
- **Cost:** $${summary.cost.toFixed(4)}

**Content Preview:**
\`\`\`markdown
${summary.content.substring(0, 500)}...
\`\`\`

`;
        });

        markdown += `### Final Enhanced Summary

**Model:** ${data.processedResults.finalSummary.model}  
**Tokens:** ${data.processedResults.finalSummary.tokens}  
**Cost:** $${data.processedResults.finalSummary.cost.toFixed(4)}

**Full Content:**
\`\`\`markdown
${data.processedResults.finalSummary.content}
\`\`\`

---

## Cost Breakdown

| Phase | Model | Calls | Tokens In | Tokens Out | Cost |
|-------|-------|-------|-----------|------------|------|`;

        // Build cost table
        const phases = {
            'parallel_generation': { calls: [], totalIn: 0, totalOut: 0, totalCost: 0 },
            'final_selection': { calls: [], totalIn: 0, totalOut: 0, totalCost: 0 }
        };

        data.modelResponses.forEach(call => {
            const phase = call.metadata.phase;
            if (phases[phase]) {
                phases[phase].calls.push(call);
                phases[phase].totalIn += call.response.usage?.prompt_tokens || 0;
                phases[phase].totalOut += call.response.usage?.completion_tokens || 0;
                phases[phase].totalCost += call.response.cost || 0;
            }
        });

        Object.entries(phases).forEach(([phase, data]) => {
            if (data.calls.length > 0) {
                const model = data.calls[0].model;
                markdown += `
| ${phase.replace('_', ' ')} | ${model} | ${data.calls.length} | ${data.totalIn.toLocaleString()} | ${data.totalOut.toLocaleString()} | $${data.totalCost.toFixed(4)} |`;
            }
        });

        markdown += `

**Total Cost:** $${(phases.parallel_generation.totalCost + phases.final_selection.totalCost).toFixed(4)}

---

*This document was automatically generated from captured API responses using json-to-markdown.js*
`;

        // Write the markdown file
        fs.writeFileSync(outputFile, markdown);
        
        console.log(`✅ Markdown generated: ${outputFile}`);
        console.log(`📄 ${markdown.split('\n').length} lines written`);
        
        return markdown;
        
    } catch (error) {
        console.error('❌ Conversion failed:', error.message);
        throw error;
    }
}

// Run if called directly
if (process.argv[2]) {
    const inputFile = process.argv[2];
    const outputFile = process.argv[3] || inputFile.replace('.json', '.md');
    jsonToMarkdown(inputFile, outputFile);
} else {
    // Default usage
    jsonToMarkdown('.data/raw-model-responses.json', '.data/RAW_MODEL_RESPONSES.md');
}

export default jsonToMarkdown;