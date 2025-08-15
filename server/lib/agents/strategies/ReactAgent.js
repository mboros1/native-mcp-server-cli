// @ts-check
/**
 * ReAct Agent Strategy
 * 
 * Implements the ReAct (Reasoning + Acting) pattern for tool use
 * Alternates between thinking, acting (tool use), and observing results
 */

/** @typedef {import('../../../types').AgentState} AgentState */
/** @typedef {import('../../../types').AgentContext} AgentContext */
/** @typedef {import('../../../types').ToolCall} ToolCall */
/** @typedef {import('../../../types').ThoughtResponse} ThoughtResponse */
/** @typedef {import('../../../types').Tool} Tool */

import { AgentStrategy } from '../core/AgentStrategy.js';

export class ReactAgent extends AgentStrategy {
    constructor(options = {}) {
        super({
            name: 'ReactAgent',
            description: 'ReAct pattern agent for tool-based problem solving',
            capabilities: ['tool-use', 'reasoning', 'observation'],
            ...options
        });
        
        this.maxToolCalls = options.maxToolCalls || 10;
        this.requireExplicitCompletion = options.requireExplicitCompletion || false;
        this.maxRetries = options.maxRetries || 3;
    }
    
    /**
     * Main execution loop
     * @param {AgentState} state - The agent state
     * @param {AgentContext} context - Execution context
     * @returns {Promise<string>} The final response
     */
    async execute(state, context) {
        const { task, model = 'kimi', tools = [], initialResponse } = context;
        
        // Initialize conversation with the task
        state.addMessage('user', task);
        
        // If we have an initial response with tool calls, process it first
        if (initialResponse && initialResponse.toolCalls && initialResponse.toolCalls.length > 0) {
            this.log('Processing initial response with tool calls');
            
            // Add the assistant's initial response
            state.addMessage('assistant', initialResponse.content, {
                toolCalls: initialResponse.toolCalls
            });
            
            // Execute the initial tool calls
            const observations = await this.act(initialResponse.toolCalls, state);
            await this.observe(observations, state, context);
        }
        
        // Main ReAct loop
        while (!state.shouldStop()) {
            try {
                // Increment iteration
                state.nextIteration();
                
                // Phase 1: Reasoning (Thought)
                const thought = await this.think(state, context);
                
                // Check if agent believes task is complete
                if (this.isComplete(thought)) {
                    return this.formatFinalAnswer(thought, state);
                }
                
                // Phase 2: Acting (Tool Selection and Execution)
                if (thought.toolCalls && thought.toolCalls.length > 0) {
                    const observations = await this.act(thought.toolCalls, state);
                    
                    // Phase 3: Observation Processing
                    await this.observe(observations, state, context);
                } else {
                    // No tool calls, but also not complete - might need prompting
                    if (this.requireExplicitCompletion) {
                        await this.promptForCompletion(state, context);
                    } else {
                        // Assume the response is the final answer
                        return this.formatFinalAnswer(thought, state);
                    }
                }
                
                // Check if we've made progress
                if (!this.hasProgress(state)) {
                    this.log('No progress detected, may be stuck');
                    state.addObservation({
                        type: 'system',
                        message: 'No progress detected in last iteration'
                    });
                }
                
            } catch (error) {
                this.log(`Error in iteration ${state.iterationCount}: ${error.message}`, 'error');
                state.errors.push(error);
                
                if (state.errors.length >= state.maxRetries) {
                    throw new Error(`Failed after ${state.errors.length} errors: ${error.message}`);
                }
            }
        }
        
        // If we exit the loop without a clear answer
        return this.handleIncomplete(state);
    }
    
    /**
     * Think phase - Call LLM to reason about the task
     * @param {AgentState} state - Current agent state
     * @param {AgentContext} context - Execution context
     * @returns {Promise<ThoughtResponse>} LLM response with reasoning
     */
    async think(state, context) {
        const { model, tools } = context;
        
        // Build conversation context
        const messages = this.buildMessages(state);
        
        // Don't inject system prompts mid-conversation - it confuses models
        // The model already knows what to do from the initial response
        
        this.log(`Thinking... (iteration ${state.iterationCount})`);
        
        let lastError = null;
        let retryCount = 0;
        
        while (retryCount < this.maxRetries) {
            try {
                // Call LLM with tools
                const response = await this.callLLM(messages, {
                    model,
                    tools: this.formatToolsForLLM(tools)
                });
                
                // Check for API error
                if (response.finishReason === 'error') {
                    retryCount++;
                    lastError = 'API returned finish_reason: error';
                    this.log(`API error (finish_reason: error), retry ${retryCount}/${this.maxRetries}`);
                    
                    if (retryCount < this.maxRetries) {
                        // Wait before retrying (exponential backoff)
                        await new Promise(resolve => setTimeout(resolve, Math.pow(2, retryCount) * 1000));
                        continue;
                    }
                }
                
                // Add assistant's response to state
                state.addMessage('assistant', response.content, {
                    toolCalls: response.toolCalls,
                    finishReason: response.finishReason
                });
                
                return response;
                
            } catch (error) {
                retryCount++;
                lastError = error.message;
                this.log(`LLM call error: ${error.message}, retry ${retryCount}/${this.maxRetries}`);
                
                if (retryCount < this.maxRetries) {
                    // Wait before retrying (exponential backoff)
                    await new Promise(resolve => setTimeout(resolve, Math.pow(2, retryCount) * 1000));
                } else {
                    throw error;
                }
            }
        }
        
        // If we've exhausted retries, return an error response
        const errorResponse = {
            content: `Failed to get a valid response after ${this.maxRetries} retries. Last error: ${lastError}`,
            toolCalls: null,
            finishReason: /** @type {'error'} */ ('error'),
            model: model // Add missing model property
        };
        
        state.addMessage('assistant', errorResponse.content, {
            finishReason: 'error'
        });
        
        return errorResponse;
    }
    
    /**
     * Act phase - Execute tool calls
     * @param {ToolCall[]} toolCalls - Tools to execute
     * @param {AgentState} state - Current state
     * @returns {Promise<Array<{toolName: string, toolCallId: string, success: boolean, result?: any, error?: string}>>}
     */
    async act(toolCalls, state) {
        const observations = [];
        
        this.log(`Executing ${toolCalls.length} tool call(s)`);
        
        for (const toolCall of toolCalls) {
            const { function: func } = toolCall;
            const { name: toolName, arguments: toolArgs } = func;
            
            this.log(`Calling tool: ${toolName}`);
            
            try {
                // Parse arguments if they're a string
                const args = typeof toolArgs === 'string' 
                    ? JSON.parse(toolArgs) 
                    : toolArgs;
                
                // Execute the tool
                const result = await this.executeTool(toolName, args);
                
                // Record the tool call and result
                state.addToolCall(toolName, args, result.result, result.error);
                
                observations.push({
                    toolName,
                    toolCallId: toolCall.id,
                    success: result.success,
                    result: result.result,
                    error: result.error
                });
                
            } catch (error) {
                this.log(`Tool execution error: ${error.message}`, 'error');
                
                observations.push({
                    toolName,
                    toolCallId: toolCall.id,
                    success: false,
                    error: error.message
                });
                
                state.addToolCall(toolName, toolArgs, null, error.message);
            }
        }
        
        return observations;
    }
    
    /**
     * Observe phase - Process tool results
     */
    async observe(observations, state, context) {
        const { model } = context;
        
        // Format observations for the LLM
        const observationMessage = this.formatObservations(observations);
        
        // Add observations to conversation as 'tool' role
        // This is what Kimi expects after tool execution
        state.addMessage('tool', observationMessage, {
            observations
        });
        
        this.log(`Processed ${observations.length} observation(s)`);
        
        // Optional: Get LLM's interpretation of the observations
        if (this.shouldInterpretObservations(observations)) {
            const interpretation = await this.interpretObservations(
                observations,
                state,
                model
            );
            
            if (interpretation) {
                state.addObservation({
                    type: 'interpretation',
                    content: interpretation
                });
            }
        }
    }
    
    /**
     * Build messages for LLM context
     */
    buildMessages(state) {
        const messages = [];
        
        // Add conversation history
        const history = state.getConversationContext();
        messages.push(...history);
        
        // Add recent observations if any
        const recentObs = state.getRecentObservations(3);
        if (recentObs.length > 0) {
            messages.push({
                role: 'system',
                content: `Recent observations:\n${JSON.stringify(recentObs, null, 2)}`
            });
        }
        
        return messages;
    }
    
    /**
     * Get reasoning prompt based on current state
     */
    getReasoningPrompt(state) {
        const basePrompt = `You are a ReAct agent. Approach this step-by-step:
1. Thought: Analyze what you know and what you need to find out
2. Action: If you need more information, use appropriate tools
3. Observation: Process the results and decide next steps

Current iteration: ${state.iterationCount}/${state.maxIterations}
Tools used so far: ${state.toolCalls.length}`;

        if (state.errors.length > 0) {
            return basePrompt + `\n\nNote: Previous attempts encountered errors. Please try a different approach.`;
        }
        
        return basePrompt;
    }
    
    /**
     * Format tools for LLM
     */
    formatToolsForLLM(tools) {
        if (!tools || tools.length === 0) return null;
        
        return tools.map(tool => ({
            type: 'function',
            function: {
                name: tool.name,
                description: tool.description,
                parameters: tool.parameters || {}
            }
        }));
    }
    
    /**
     * Format observations for conversation
     */
    formatObservations(observations) {
        const formatted = observations.map(obs => {
            if (obs.success) {
                return `Tool "${obs.toolName}" succeeded:\n${JSON.stringify(obs.result, null, 2)}`;
            } else {
                return `Tool "${obs.toolName}" failed: ${obs.error}`;
            }
        });
        
        return formatted.join('\n\n');
    }
    
    /**
     * Check if the agent believes the task is complete
     * @param {ThoughtResponse} thought - The LLM's response
     * @returns {boolean} Whether the task is complete
     */
    isComplete(thought) {
        // Use finish_reason if available (Kimi API standard)
        if (thought.finishReason === 'stop') {
            return true;
        }
        
        // If finish_reason is 'tool_calls', we need to continue
        if (thought.finishReason === 'tool_calls') {
            return false;
        }
        
        // Fallback: No more tool calls and response seems final
        if (!thought.toolCalls || thought.toolCalls.length === 0) {
            const content = thought.content.toLowerCase();
            
            const completionIndicators = [
                'task is complete',
                'successfully completed',
                'here is the answer',
                'here are the results',
                'in summary',
                'to summarize',
                'final answer',
                'based on the information'
            ];
            
            return completionIndicators.some(indicator => 
                content.includes(indicator)
            );
        }
        
        return false;
    }
    
    /**
     * Check if we should interpret observations
     */
    shouldInterpretObservations(observations) {
        // Interpret if there are multiple observations or failures
        return observations.length > 1 || 
               observations.some(obs => !obs.success);
    }
    
    /**
     * Get LLM interpretation of observations
     */
    async interpretObservations(observations, state, model) {
        const prompt = `Based on these tool results, what have we learned?\n${this.formatObservations(observations)}`;
        
        try {
            const response = await this.callLLM([
                { role: 'system', content: 'Briefly summarize what these tool results tell us.' },
                { role: 'user', content: prompt }
            ], { model });
            
            return response.content;
        } catch (error) {
            this.log(`Failed to interpret observations: ${error.message}`, 'error');
            return null;
        }
    }
    
    /**
     * Check if the agent is making progress
     */
    hasProgress(state) {
        // Check if we've had new tool calls or observations
        const recentCalls = state.toolCalls.slice(-2);
        if (recentCalls.length < 2) return true;
        
        // Check if we're calling the same tool with same args
        const [prev, curr] = recentCalls;
        return !(
            prev.toolName === curr.toolName &&
            JSON.stringify(prev.args) === JSON.stringify(curr.args)
        );
    }
    
    /**
     * Prompt for explicit completion
     */
    async promptForCompletion(state, context) {
        const prompt = "Based on the information gathered, please provide a final answer to the original question.";
        
        state.addMessage('system', prompt);
        
        const response = await this.callLLM(
            this.buildMessages(state),
            { model: context.model }
        );
        
        state.addMessage('assistant', response.content);
    }
    
    /**
     * Format the final answer
     */
    formatFinalAnswer(thought, state) {
        // Log stats for debugging
        this.log(`Final answer stats: tools=${state.toolCalls.length}, iterations=${state.iterationCount}`);
        
        // Return just the content string for compatibility
        return thought.content;
    }
    
    /**
     * Handle incomplete execution
     */
    handleIncomplete(state) {
        const lastMessage = state.conversationHistory[state.conversationHistory.length - 1];
        
        this.log(`Incomplete execution: tools=${state.toolCalls.length}, iterations=${state.iterationCount}`);
        
        // Return just the message string
        return lastMessage?.content || 'Unable to complete the task within iteration limits';
    }
    
    /**
     * Check if this strategy can handle the task
     */
    canHandle(task) {
        // ReactAgent is good for tasks that likely need tool use
        const toolIndicators = [
            'check', 'find', 'search', 'look up', 'get',
            'list', 'show', 'what', 'which', 'where',
            'analyze', 'examine', 'inspect'
        ];
        
        const taskLower = task.toLowerCase();
        return toolIndicators.some(indicator => taskLower.includes(indicator));
    }
}

export default ReactAgent;