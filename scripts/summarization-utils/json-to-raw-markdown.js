import fs from 'fs';

/**
 * Completely raw JSON to Markdown converter
 * No truncation, no summarization - everything as-is
 */

function jsonToRawMarkdown(inputFile, outputFile) {
    console.log(`🔄 Converting ${inputFile} to RAW markdown: ${outputFile}...`);
    
    try {
        const data = JSON.parse(fs.readFileSync(inputFile, 'utf8'));
        
        let markdown = `# Raw Model Responses - Complete Untruncated Data

*Source: ${inputFile}*  
*Generated: ${new Date().toISOString()}*

---

## Metadata

\`\`\`json
${JSON.stringify(data.metadata, null, 2)}
\`\`\`

---

## Input Data

### Input Stats
\`\`\`json
${JSON.stringify(data.metadata.inputStats, null, 2)}
\`\`\`

### Configuration Used
\`\`\`json
${JSON.stringify(data.metadata.configuration, null, 2)}
\`\`\`

---

## Complete Model API Calls

`;

        // Raw dump of every API call
        (data.modelResponses || []).forEach((call, i) => {
            markdown += `### ${call.callId}: ${call.model}

#### Complete Request Data
\`\`\`json
${JSON.stringify(call.request, null, 2)}
\`\`\`

#### Complete Response Data  
\`\`\`json
${JSON.stringify(call.response, null, 2)}
\`\`\`

#### Call Metadata
\`\`\`json
${JSON.stringify({
    callId: call.callId,
    timestamp: call.timestamp,
    duration: call.duration,
    model: call.model,
    metadata: call.metadata
}, null, 2)}
\`\`\`

---

`;
        });

        markdown += `## Processed Results - Complete

### All Parallel Summaries
\`\`\`json
${JSON.stringify(data.results?.parallelSummaries || [], null, 2)}
\`\`\`

### Final Enhanced Summary
\`\`\`json
${JSON.stringify(data.results?.finalSummary || {}, null, 2)}
\`\`\`

---

## Complete Dataset JSON Export

<details>
<summary>Click to expand complete raw JSON data</summary>

\`\`\`json
${JSON.stringify(data, null, 2)}
\`\`\`

</details>

---

*This is a completely untruncated dump of all captured model interaction data.*
`;

        fs.writeFileSync(outputFile, markdown);
        
        console.log(`✅ Raw markdown generated: ${outputFile}`);
        console.log(`📄 ${markdown.split('\n').length} lines written (untruncated)`);
        
        return markdown;
        
    } catch (error) {
        console.error('❌ Raw conversion failed:', error.message);
        throw error;
    }
}

export default jsonToRawMarkdown;

// Run if called directly
if (process.argv[2]) {
    const inputFile = process.argv[2];
    const outputFile = process.argv[3] || inputFile.replace('.json', '_RAW.md');
    jsonToRawMarkdown(inputFile, outputFile);
}