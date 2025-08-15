// @ts-check
/**
 * Base Agent Strategy
 * 
 * Abstract base class for different agent execution strategies
 */

import { log } from '../../logger.js';

export class AgentStrategy {
    constructor(options = {}) {
        this.name = options.name || this.constructor.name;
        this.description = options.description || 'Base agent strategy';
        this.capabilities = options.capabilities || [];
        
        // Configuration
        this.maxIterations = options.maxIterations || 15;
        this.temperature = options.temperature || 0.7;
        this.verbose = options.verbose || false;
        
        // Dependencies (injected)
        this.apiManager = null;
        this.toolExecutor = null;
        this.memoryManager = null;
    }
    
    /**
     * Main execution method - must be implemented by subclasses
     * @param {import('../../../types').AgentState} state - Agent state
     * @param {import('../../../types').AgentContext} context - Execution context
     * @returns {Promise<string>} The final result/response
     */
    async execute(state, context) {
        throw new Error(`${this.name} must implement execute() method`);
    }
    
    /**
     * Initialize the strategy with dependencies
     */
    initialize(dependencies) {
        this.apiManager = dependencies.apiManager;
        this.toolExecutor = dependencies.toolExecutor;
        this.memoryManager = dependencies.memoryManager;
        return this;
    }
    
    /**
     * Check if this strategy can handle the given task
     */
    canHandle(task) {
        // Override in subclasses for specific matching logic
        return true;
    }
    
    /**
     * Pre-execution hook
     */
    async beforeExecute(state, context) {
        this.log(`Starting execution for task: ${context.task?.substring(0, 50)}...`);
        state.status = 'executing';
        state.checkpoint('before_execute');
    }
    
    /**
     * Post-execution hook
     * @param {import('../../../types').AgentState} state - Agent state
     * @param {string} result - Execution result
     * @returns {Promise<string>} The result (passed through)
     */
    async afterExecute(state, result) {
        this.log(`Execution completed. Stats: ${JSON.stringify(state.getStats())}`);
        state.status = 'completed';
        state.checkpoint('after_execute');
        return result;
    }
    
    /**
     * Error handling hook
     */
    async onError(state, error) {
        this.log(`Error during execution: ${error.message}`, 'error');
        state.errors.push({
            error: error.message,
            stack: error.stack,
            timestamp: Date.now(),
            iteration: state.iterationCount
        });
        state.status = 'failed';
        
        // Decide whether to retry
        if (state.retryCount < state.maxRetries) {
            state.retryCount++;
            this.log(`Retrying... (attempt ${state.retryCount}/${state.maxRetries})`);
            return 'retry';
        }
        
        return 'fail';
    }
    
    /**
     * Call the LLM with the current context
     */
    async callLLM(messages, options = {}) {
        const {
            model = 'kimi',
            tools = null,
            temperature = this.temperature,
            maxTokens = 4096
        } = options;
        
        this.log(`Calling LLM (${model}) with ${messages.length} messages`);
        
        // Log message details for debugging
        if (process.env.DEBUG_AGENTS === 'true') {
            messages.forEach((msg, i) => {
                const preview = msg.content.substring(0, 100).replace(/\n/g, ' ');
                this.log(`  Message ${i}: role=${msg.role}, length=${msg.content.length}, preview="${preview}..."`);
            });
        }
        
        try {
            // Find the last user message to use as the main content
            // If the last message is a system message (like tool results), 
            // we need to add a prompt for the model to continue
            let mainContent;
            let history;
            
            const lastMessage = messages[messages.length - 1];
            if (lastMessage.role === 'system') {
                // After tool execution, merge tool results into a user message
                // Kimi doesn't like system messages in the middle of conversation
                mainContent = lastMessage.content + "\n\nPlease continue with the task based on these results.";
                // Exclude the system message from history
                history = messages.slice(0, -1);
            } else if (lastMessage.role === 'user') {
                // Normal case - last message is from user
                mainContent = lastMessage.content;
                history = messages.slice(0, -1);
            } else {
                // Assistant message - shouldn't happen but handle it
                mainContent = "Please continue.";
                history = messages;
            }
            
            const response = await this.apiManager.sendMessage(
                mainContent,
                model,
                null, // reasoning_effort
                60000, // timeout
                null, // abortSignal
                history,
                tools
            );
            
            // Pass through all response fields including finishReason
            return response;
        } catch (error) {
            this.log(`LLM call failed: ${error.message}`, 'error');
            throw error;
        }
    }
    
    /**
     * Execute a tool
     */
    async executeTool(toolName, args) {
        this.log(`Executing tool: ${toolName}`);
        
        try {
            const result = await this.toolExecutor(toolName, args);
            this.log(`Tool ${toolName} completed successfully`);
            return { success: true, result };
        } catch (error) {
            this.log(`Tool ${toolName} failed: ${error.message}`, 'error');
            return { success: false, error: error.message };
        }
    }
    
    /**
     * Format tool results for the LLM
     */
    formatToolResults(toolResults) {
        return toolResults.map(result => {
            if (result.success) {
                return `Tool ${result.toolName} succeeded:\n${JSON.stringify(result.result, null, 2)}`;
            } else {
                return `Tool ${result.toolName} failed: ${result.error}`;
            }
        }).join('\n\n');
    }
    
    /**
     * Check if the agent has found an answer
     */
    hasAnswer(state) {
        // Check if the last message indicates completion
        const lastMessage = state.conversationHistory[state.conversationHistory.length - 1];
        if (!lastMessage) return false;
        
        // Look for completion indicators
        const completionPhrases = [
            'task is complete',
            'task has been completed',
            'successfully completed',
            'here is the answer',
            'here are the results',
            'to summarize'
        ];
        
        const content = lastMessage.content.toLowerCase();
        return completionPhrases.some(phrase => content.includes(phrase));
    }
    
    /**
     * Logging helper
     */
    log(message, level = 'info') {
        const prefix = `[${this.name}]`;
        
        if (this.verbose || level === 'error') {
            log(`${prefix} ${message}`);
        }
    }
    
    /**
     * Get strategy metadata
     */
    getMetadata() {
        return {
            name: this.name,
            description: this.description,
            capabilities: this.capabilities,
            maxIterations: this.maxIterations
        };
    }
}

export default AgentStrategy;