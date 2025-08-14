/**
 * Agent State Manager
 * 
 * Manages the state of an agent execution including conversation history,
 * todo lists, memory, and checkpoints
 */

export class AgentState {
    constructor(options = {}) {
        // Core state
        this.conversationHistory = [];
        this.todoList = [];
        this.currentPlan = null;
        
        // Execution tracking
        this.iterationCount = 0;
        this.maxIterations = options.maxIterations || 20;
        this.startTime = Date.now();
        this.status = 'initialized'; // initialized, planning, executing, completed, failed
        
        // Memory stores
        this.shortTermMemory = [];  // Current execution context
        this.workingMemory = new Map();  // Key-value store for current task
        
        // Checkpointing
        this.checkpoints = [];
        this.lastCheckpoint = null;
        
        // Tool execution history
        this.toolCalls = [];
        this.observations = [];
        
        // Error tracking
        this.errors = [];
        this.retryCount = 0;
        this.maxRetries = options.maxRetries || 3;
    }
    
    /**
     * Add a message to conversation history
     */
    addMessage(role, content, metadata = {}) {
        const message = {
            role,
            content,
            timestamp: Date.now(),
            iteration: this.iterationCount,
            ...metadata
        };
        this.conversationHistory.push(message);
        return message;
    }
    
    /**
     * Add a tool call and its result
     */
    addToolCall(toolName, args, result, error = null) {
        const toolCall = {
            toolName,
            args,
            result,
            error,
            timestamp: Date.now(),
            iteration: this.iterationCount
        };
        this.toolCalls.push(toolCall);
        
        // Also add as observation
        this.addObservation({
            type: 'tool_result',
            tool: toolName,
            success: !error,
            data: error || result
        });
        
        return toolCall;
    }
    
    /**
     * Add an observation (tool result, reflection, etc)
     */
    addObservation(observation) {
        const obs = {
            ...observation,
            timestamp: Date.now(),
            iteration: this.iterationCount
        };
        this.observations.push(obs);
        this.shortTermMemory.push(obs);
        
        // Keep short-term memory bounded
        if (this.shortTermMemory.length > 10) {
            this.shortTermMemory.shift();
        }
        
        return obs;
    }
    
    /**
     * Create a checkpoint of current state
     */
    checkpoint(label = '') {
        const checkpoint = {
            id: this.checkpoints.length,
            label,
            timestamp: Date.now(),
            iteration: this.iterationCount,
            state: {
                conversationHistory: [...this.conversationHistory],
                todoList: [...this.todoList],
                workingMemory: new Map(this.workingMemory),
                status: this.status
            }
        };
        
        this.checkpoints.push(checkpoint);
        this.lastCheckpoint = checkpoint;
        return checkpoint;
    }
    
    /**
     * Restore from a checkpoint
     */
    restore(checkpointId) {
        const checkpoint = this.checkpoints.find(cp => cp.id === checkpointId);
        if (!checkpoint) {
            throw new Error(`Checkpoint ${checkpointId} not found`);
        }
        
        this.conversationHistory = [...checkpoint.state.conversationHistory];
        this.todoList = [...checkpoint.state.todoList];
        this.workingMemory = new Map(checkpoint.state.workingMemory);
        this.status = checkpoint.state.status;
        this.iterationCount = checkpoint.iteration;
        
        return checkpoint;
    }
    
    /**
     * Increment iteration and check limits
     */
    nextIteration() {
        this.iterationCount++;
        if (this.iterationCount >= this.maxIterations) {
            throw new Error(`Maximum iterations (${this.maxIterations}) reached`);
        }
        return this.iterationCount;
    }
    
    /**
     * Check if we should stop execution
     */
    shouldStop() {
        return (
            this.status === 'completed' ||
            this.status === 'failed' ||
            this.iterationCount >= this.maxIterations ||
            this.errors.length > this.maxRetries
        );
    }
    
    /**
     * Get execution statistics
     */
    getStats() {
        return {
            iterations: this.iterationCount,
            toolCalls: this.toolCalls.length,
            observations: this.observations.length,
            errors: this.errors.length,
            duration: Date.now() - this.startTime,
            status: this.status,
            checkpoints: this.checkpoints.length
        };
    }
    
    /**
     * Get the last N observations
     */
    getRecentObservations(n = 5) {
        return this.observations.slice(-n);
    }
    
    /**
     * Get conversation context for LLM
     */
    getConversationContext() {
        // Format conversation for LLM consumption
        return this.conversationHistory.map(msg => ({
            role: msg.role,
            content: msg.content
        }));
    }
    
    /**
     * Store a value in working memory
     */
    remember(key, value) {
        this.workingMemory.set(key, value);
    }
    
    /**
     * Retrieve from working memory
     */
    recall(key) {
        return this.workingMemory.get(key);
    }
    
    /**
     * Clear working memory
     */
    clearMemory() {
        this.workingMemory.clear();
        this.shortTermMemory = [];
    }
}

export default AgentState;