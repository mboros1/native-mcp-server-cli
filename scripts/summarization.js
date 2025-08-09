import axios from 'axios';
import { config } from 'dotenv';

config();

/**
 * Advanced Conversation Summarization Module
 * 
 * Implements multi-stage ensemble summarization with intelligent selection:
 * 1. Generate 3 parallel summaries using cost-effective models  
 * 2. Use higher-quality model to select best summary and enhance with missing context
 * 3. Optimized for cost/quality balance and long conversation handling
 */
class ConversationSummarizer {
  constructor(options = {}) {
    this.apiKey = options.apiKey || process.env.OPENAI_API_KEY;
    this.baseURL = options.baseURL || 'https://api.openai.com/v1';
    this.maxRetries = options.maxRetries || 3;
    this.retryDelay = options.retryDelay || 1000;
    
    // Model configuration optimized for cost/performance
    this.models = {
      cheap: options.cheapModel || 'gpt-4.1-nano',  // For parallel summaries
      quality: options.qualityModel || 'gpt-4.1',   // For final selection
      fallback: options.fallbackModel || 'gpt-4o-mini' // If 4.1 unavailable
    };
    
    // Token management
    this.triggerTokens = options.triggerTokens || 40000;
    this.targetSummaryTokens = options.targetSummaryTokens || 4000;
    this.compressionRatio = options.compressionRatio || 10; // 40k -> 4k
    
    this.client = axios.create({
      baseURL: this.baseURL,
      headers: {
        'Authorization': `Bearer ${this.apiKey}`,
        'Content-Type': 'application/json'
      },
      timeout: 60000 // 60 second timeout
    });
    
    this.log = options.logger || console;
  }

  /**
   * Count approximate tokens (rough estimation: 1 token ≈ 4 characters)
   */
  estimateTokens(text) {
    return Math.ceil(text.length / 4);
  }

  /**
   * Make OpenAI API call with retry logic
   */
  async makeOpenAICall(model, messages, maxTokens = null, temperature = 0.7) {
    const payload = {
      model,
      messages
    };
    
    // o4-mini and other reasoning models have specific parameter requirements
    if (model.includes('o4')) {
      // o4 models only support temperature=1 and max_completion_tokens only
      payload.temperature = 1;
      if (maxTokens) {
        payload.max_completion_tokens = maxTokens;  // Limit visible output tokens only
      }
    } else {
      // Standard models support custom temperature and use max_tokens
      payload.temperature = temperature;
      if (maxTokens) {
        payload.max_tokens = maxTokens;
      }
    }

    for (let attempt = 1; attempt <= this.maxRetries; attempt++) {
      try {
        const response = await this.client.post('/chat/completions', payload);
        
        const result = {
          content: response.data.choices[0].message.content,
          usage: response.data.usage,
          model: response.data.model,
          finishReason: response.data.choices[0].finish_reason
        };
        
        this.log.debug(`OpenAI API call successful (attempt ${attempt}):`, {
          model,
          inputTokens: result.usage.prompt_tokens,
          outputTokens: result.usage.completion_tokens,
          totalTokens: result.usage.total_tokens
        });
        
        return result;
        
      } catch (error) {
        const isLastAttempt = attempt === this.maxRetries;
        
        if (error.response?.status === 429) {
          // Rate limit - exponential backoff
          const delay = this.retryDelay * Math.pow(2, attempt - 1);
          this.log.warn(`Rate limited, retrying in ${delay}ms (attempt ${attempt}/${this.maxRetries})`);
          
          if (!isLastAttempt) {
            await new Promise(resolve => setTimeout(resolve, delay));
            continue;
          }
        }
        
        if (error.response?.status === 400 && error.response?.data?.error?.code === 'context_length_exceeded') {
          throw new Error('Context length exceeded - conversation too long for model');
        }
        
        if (isLastAttempt) {
          this.log.error('OpenAI API call failed after all retries:', {
            status: error.response?.status,
            error: error.response?.data?.error || error.message
          });
          throw error;
        }
        
        this.log.warn(`API call failed, retrying (attempt ${attempt}/${this.maxRetries}):`, error.response?.data?.error || error.message);
        await new Promise(resolve => setTimeout(resolve, this.retryDelay));
      }
    }
  }

  /**
   * Generate a single summary with specific focus/style
   */
  async generateSummary(conversationText, summaryIndex, customPrompt = null) {
    const basePrompts = [
      // Summary 1: Comprehensive overview
      `Create a comprehensive ${this.targetSummaryTokens}-token summary of this conversation focusing on:
1. Key decisions and action items
2. Important technical details and code changes
3. Context needed for future conversations  
4. Any unresolved questions or concerns

Maintain conversational flow and preserve critical information for continuity.`,

      // Summary 2: Technical focus
      `Create a detailed ${this.targetSummaryTokens}-token summary emphasizing:
1. Technical implementations and code modifications
2. System architecture decisions
3. Problem-solving approaches and solutions
4. Development workflow and process changes

Keep technical accuracy and implementation details.`,

      // Summary 3: Strategic focus  
      `Create a strategic ${this.targetSummaryTokens}-token summary highlighting:
1. High-level decisions and their rationale
2. Project direction and goals
3. Important context and background information
4. Next steps and future considerations

Focus on the bigger picture and strategic context.`
    ];

    const prompt = customPrompt || basePrompts[summaryIndex % basePrompts.length];
    
    const messages = [
      {
        role: 'system',
        content: 'You are an expert at creating high-quality conversation summaries that preserve important context and maintain readability.'
      },
      {
        role: 'user', 
        content: `${prompt}\n\nCONVERSATION TO SUMMARIZE:\n\n${conversationText}`
      }
    ];

    try {
      const result = await this.makeOpenAICall(
        this.models.cheap,
        messages,
        this.targetSummaryTokens * 1.2, // Allow 20% buffer
        0.3 // Lower temperature for more focused summaries
      );
      
      return {
        summary: result.content,
        tokens: result.usage.completion_tokens,
        focus: ['comprehensive', 'technical', 'strategic'][summaryIndex % 3],
        cost: this.calculateCost(this.models.cheap, result.usage)
      };
      
    } catch (error) {
      this.log.error(`Failed to generate summary ${summaryIndex + 1}:`, error.message);
      throw error;
    }
  }

  /**
   * Generate multiple summaries in parallel
   */
  async generateParallelSummaries(conversationText, count = 3) {
    this.log.info(`Generating ${count} parallel summaries using ${this.models.cheap}`);
    
    const summaryPromises = Array.from({ length: count }, (_, i) => 
      this.generateSummary(conversationText, i)
    );

    try {
      const summaries = await Promise.all(summaryPromises);
      
      const totalCost = summaries.reduce((sum, s) => sum + s.cost, 0);
      const totalTokens = summaries.reduce((sum, s) => sum + s.tokens, 0);
      
      this.log.info(`Generated ${count} summaries:`, {
        totalOutputTokens: totalTokens,
        averageLength: Math.round(totalTokens / count),
        totalCost: `$${totalCost.toFixed(4)}`
      });
      
      return summaries;
      
    } catch (error) {
      this.log.error('Failed to generate parallel summaries:', error.message);
      throw error;
    }
  }

  /**
   * Use high-quality model to select best summary and enhance it
   */
  async selectAndEnhanceSummary(originalText, summaries) {
    this.log.info(`Using ${this.models.quality} to select and enhance best summary`);
    
    const summaryTexts = summaries.map((s, i) => 
      `=== SUMMARY ${i + 1} (${s.focus.toUpperCase()}) ===\n${s.summary}`
    ).join('\n\n');

    const messages = [
      {
        role: 'system',
        content: 'You are an expert editor who can identify the highest quality summaries and enhance them with missing critical context.'
      },
      {
        role: 'user',
        content: `Review these ${summaries.length} summaries of our conversation and create the best possible final summary.

TASK:
1. Identify which summary captures the most critical information
2. Determine what important context might be missing from your chosen summary  
3. Create an enhanced ${this.targetSummaryTokens}-token summary that:
   - Takes the best elements from all summaries
   - Adds any critically missing context from the original conversation
   - Maintains perfect continuity for future conversations
   - Preserves technical accuracy and important decisions

ORIGINAL CONVERSATION:
${originalText}

CANDIDATE SUMMARIES:
${summaryTexts}

Create the final enhanced summary:`
      }
    ];

    try {
      const result = await this.makeOpenAICall(
        this.models.quality,
        messages,
        this.targetSummaryTokens * 1.2,
        0.2 // Lower temperature for consistent quality
      );
      
      const cost = this.calculateCost(this.models.quality, result.usage);
      
      this.log.info('Final summary generated:', {
        outputTokens: result.usage.completion_tokens,
        cost: `$${cost.toFixed(4)}`,
        model: result.model
      });
      
      return {
        summary: result.content,
        tokens: result.usage.completion_tokens,
        cost,
        model: result.model,
        usage: result.usage
      };
      
    } catch (error) {
      this.log.error('Failed to select and enhance summary:', error.message);
      throw error;
    }
  }

  /**
   * Calculate estimated cost based on model and token usage
   */
  calculateCost(model, usage) {
    // Pricing per 1M tokens (as of 2025)
    const pricing = {
      'gpt-4.1': { input: 2.00, output: 8.00 },
      'gpt-4.1-mini': { input: 0.40, output: 1.60 },
      'gpt-4.1-nano': { input: 0.10, output: 0.40 },
      'gpt-4o-mini': { input: 0.15, output: 0.60 },
      'gpt-4o': { input: 2.50, output: 10.00 },
      'o4-mini': { input: 1.10, output: 4.40 }  // Reasoning model pricing
    };
    
    const modelPricing = pricing[model] || pricing['gpt-4o-mini'];
    const inputCost = (usage.prompt_tokens / 1000000) * modelPricing.input;
    const outputCost = (usage.completion_tokens / 1000000) * modelPricing.output;
    
    return inputCost + outputCost;
  }

  /**
   * Main summarization method - implements the full ensemble strategy
   */
  async summarizeConversation(conversationText, options = {}) {
    const startTime = Date.now();
    const inputTokens = this.estimateTokens(conversationText);
    
    this.log.info('Starting ensemble summarization:', {
      inputTokens,
      inputLength: conversationText.length,
      targetSummaryTokens: this.targetSummaryTokens,
      compressionRatio: `${inputTokens}:${this.targetSummaryTokens} (${Math.round(inputTokens/this.targetSummaryTokens)}:1)`
    });

    try {
      // Phase 1: Generate multiple summaries in parallel
      const summaries = await this.generateParallelSummaries(
        conversationText, 
        options.summaryCount || 3
      );

      // Phase 2: Select and enhance the best summary
      const finalSummary = await this.selectAndEnhanceSummary(conversationText, summaries);

      const totalTime = Date.now() - startTime;
      const totalCost = summaries.reduce((sum, s) => sum + s.cost, 0) + finalSummary.cost;

      const result = {
        summary: finalSummary.summary,
        metadata: {
          originalTokens: inputTokens,
          summaryTokens: finalSummary.tokens,
          compressionRatio: Math.round(inputTokens / finalSummary.tokens * 10) / 10,
          totalCost: totalCost,
          processingTimeMs: totalTime,
          models: {
            parallel: this.models.cheap,
            final: this.models.quality
          },
          candidateSummaries: summaries.length,
          costBreakdown: {
            parallelPhase: summaries.reduce((sum, s) => sum + s.cost, 0),
            finalPhase: finalSummary.cost
          }
        }
      };

      this.log.info('Summarization completed successfully:', {
        compressionRatio: `${result.metadata.compressionRatio}:1`,
        totalCost: `$${totalCost.toFixed(4)}`,
        processingTime: `${totalTime}ms`,
        outputTokens: finalSummary.tokens
      });

      return result;

    } catch (error) {
      this.log.error('Summarization failed:', error.message);
      throw new Error(`Conversation summarization failed: ${error.message}`);
    }
  }

  /**
   * Check if conversation needs summarization based on token count
   */
  shouldSummarize(conversationText) {
    const tokens = this.estimateTokens(conversationText);
    return tokens >= this.triggerTokens;
  }

  /**
   * Progressive summarization for very long conversations
   * Maintains recent context + summarized older context
   */
  async progressiveSummarize(conversationHistory, options = {}) {
    const recentTokenLimit = options.recentTokens || this.triggerTokens;
    let processedHistory = [];
    let totalCost = 0;

    // Split conversation into chunks
    const chunks = this.splitConversationIntoChunks(conversationHistory, this.triggerTokens);
    
    for (let i = 0; i < chunks.length - 1; i++) { // Keep last chunk as recent context
      const chunk = chunks[i];
      this.log.info(`Summarizing conversation chunk ${i + 1}/${chunks.length - 1}`);
      
      const result = await this.summarizeConversation(chunk, options);
      processedHistory.push({
        type: 'summary',
        content: result.summary,
        originalTokens: result.metadata.originalTokens,
        summaryTokens: result.metadata.summaryTokens,
        timestamp: new Date().toISOString()
      });
      
      totalCost += result.metadata.totalCost;
    }

    // Add recent context as-is
    if (chunks.length > 0) {
      processedHistory.push({
        type: 'recent',
        content: chunks[chunks.length - 1],
        tokens: this.estimateTokens(chunks[chunks.length - 1]),
        timestamp: new Date().toISOString()
      });
    }

    return {
      processedHistory,
      totalCost,
      compressionAchieved: conversationHistory.length - processedHistory.reduce((sum, item) => sum + item.content.length, 0)
    };
  }

  /**
   * Split long conversation into manageable chunks
   */
  splitConversationIntoChunks(text, chunkSize) {
    const tokens = this.estimateTokens(text);
    if (tokens <= chunkSize) return [text];

    // Split by sentences/paragraphs to maintain context
    const sentences = text.split(/(?<=[.!?])\s+/);
    const chunks = [];
    let currentChunk = '';
    
    for (const sentence of sentences) {
      const testChunk = currentChunk + (currentChunk ? ' ' : '') + sentence;
      if (this.estimateTokens(testChunk) > chunkSize && currentChunk) {
        chunks.push(currentChunk);
        currentChunk = sentence;
      } else {
        currentChunk = testChunk;
      }
    }
    
    if (currentChunk) chunks.push(currentChunk);
    return chunks;
  }
}

export default ConversationSummarizer;