/**
 * Agent Orchestrator
 * 
 * Main orchestrator for managing different agent strategies and execution
 */

import { log } from '../../logger.js';
import { AgentState } from './AgentState.js';

export class AgentOrchestrator {
    constructor(options = {}) {
        // Strategy registry
        this.strategies = new Map();
        this.defaultStrategy = options.defaultStrategy || 'react';
        
        // Dependencies
        this.apiManager = options.apiManager;
        this.toolExecutor = options.toolExecutor;
        this.memoryManager = options.memoryManager;
        
        // Lifecycle hooks
        this.hooks = new Map([
            ['beforeRun', []],
            ['afterRun', []],
            ['onError', []],
            ['onToolCall', []],
            ['onIteration', []]
        ]);
        
        // Middleware for request/response transformation
        this.middleware = [];
        
        // Configuration
        this.config = {
            maxGlobalIterations: options.maxGlobalIterations || 50,
            enableCheckpointing: options.enableCheckpointing !== false,
            enableMemory: options.enableMemory !== false,
            verbose: options.verbose || false
        };
        
        // Statistics
        this.stats = {
            totalRuns: 0,
            successfulRuns: 0,
            failedRuns: 0,
            totalTokens: 0
        };
    }
    
    /**
     * Register a strategy
     */
    registerStrategy(name, strategy) {
        if (!strategy.execute || typeof strategy.execute !== 'function') {
            throw new Error('Strategy must implement execute() method');
        }
        
        // Initialize strategy with dependencies
        strategy.initialize({
            apiManager: this.apiManager,
            toolExecutor: this.toolExecutor,
            memoryManager: this.memoryManager
        });
        
        this.strategies.set(name, strategy);
        this.log(`Registered strategy: ${name}`);
        
        return this;
    }
    
    /**
     * Select the appropriate strategy for a task
     */
    selectStrategy(task, explicitStrategy = null) {
        // If strategy is explicitly specified
        if (explicitStrategy && this.strategies.has(explicitStrategy)) {
            return this.strategies.get(explicitStrategy);
        }
        
        // Try to auto-select based on task characteristics
        for (const [name, strategy] of this.strategies) {
            if (strategy.canHandle && strategy.canHandle(task)) {
                this.log(`Auto-selected strategy: ${name}`);
                return strategy;
            }
        }
        
        // Fall back to default
        const defaultStrat = this.strategies.get(this.defaultStrategy);
        if (defaultStrat) {
            this.log(`Using default strategy: ${this.defaultStrategy}`);
            return defaultStrat;
        }
        
        throw new Error('No suitable strategy found for task');
    }
    
    /**
     * Main execution method
     */
    async run(task, options = {}) {
        const {
            strategy: strategyName = null,
            model = 'kimi',
            maxIterations = null,
            context = {},
            stream = false
        } = options;
        
        this.stats.totalRuns++;
        
        try {
            // Run beforeRun hooks
            await this.runHooks('beforeRun', { task, options });
            
            // Create new state
            const state = new AgentState({
                maxIterations: maxIterations || this.config.maxGlobalIterations
            });
            
            // Select strategy
            const strategy = this.selectStrategy(task, strategyName);
            
            // Prepare execution context
            const executionContext = {
                task,
                model,
                stream,
                ...context,
                orchestrator: this
            };
            
            // Apply middleware to task
            let processedTask = task;
            for (const mw of this.middleware) {
                if (mw.processTask) {
                    processedTask = await mw.processTask(processedTask, state);
                }
            }
            executionContext.task = processedTask;
            
            // Execute strategy
            this.log(`Starting execution with strategy: ${strategy.name}`);
            
            // Pre-execution
            if (strategy.beforeExecute) {
                await strategy.beforeExecute(state, executionContext);
            }
            
            let result;
            try {
                // Main execution
                result = await this.executeWithMonitoring(
                    strategy,
                    state,
                    executionContext
                );
                
                // Post-execution
                if (strategy.afterExecute) {
                    result = await strategy.afterExecute(state, result);
                }
                
            } catch (error) {
                // Error handling
                if (strategy.onError) {
                    const action = await strategy.onError(state, error);
                    if (action === 'retry' && state.retryCount < state.maxRetries) {
                        // Don't create a new run - that causes infinite loops
                        // Instead, let the strategy handle retries internally
                        this.log(`Strategy requested retry but orchestrator preventing infinite loop`);
                    }
                }
                throw error;
            }
            
            // Apply middleware to result
            for (const mw of this.middleware) {
                if (mw.processResult) {
                    result = await mw.processResult(result, state);
                }
            }
            
            // Run afterRun hooks
            await this.runHooks('afterRun', { task, result, state });
            
            this.stats.successfulRuns++;
            
            // Return comprehensive result
            return {
                success: true,
                result,
                stats: state.getStats(),
                strategy: strategy.name,
                checkpoints: this.config.enableCheckpointing ? state.checkpoints : []
            };
            
        } catch (error) {
            this.stats.failedRuns++;
            
            // Run error hooks
            await this.runHooks('onError', { task, error });
            
            this.log(`Execution failed: ${error.message}`, 'error');
            
            return {
                success: false,
                error: error.message,
                strategy: strategyName || this.defaultStrategy
            };
        }
    }
    
    /**
     * Execute strategy with monitoring
     */
    async executeWithMonitoring(strategy, state, context) {
        const startTime = Date.now();
        
        // Set up iteration monitoring
        const iterationInterval = setInterval(async () => {
            await this.runHooks('onIteration', {
                iteration: state.iterationCount,
                stats: state.getStats()
            });
            
            // Check for runaway execution
            if (state.iterationCount > this.config.maxGlobalIterations) {
                clearInterval(iterationInterval);
                throw new Error('Global iteration limit exceeded');
            }
        }, 1000);
        
        try {
            // Execute the strategy
            const result = await strategy.execute(state, context);
            
            clearInterval(iterationInterval);
            
            const duration = Date.now() - startTime;
            this.log(`Execution completed in ${duration}ms`);
            
            return result;
            
        } catch (error) {
            clearInterval(iterationInterval);
            throw error;
        }
    }
    
    /**
     * Register a hook
     */
    registerHook(event, callback) {
        if (!this.hooks.has(event)) {
            throw new Error(`Unknown hook event: ${event}`);
        }
        
        this.hooks.get(event).push(callback);
        return this;
    }
    
    /**
     * Run hooks for an event
     */
    async runHooks(event, data) {
        const hooks = this.hooks.get(event) || [];
        
        for (const hook of hooks) {
            try {
                await hook(data);
            } catch (error) {
                this.log(`Hook error (${event}): ${error.message}`, 'error');
            }
        }
    }
    
    /**
     * Add middleware
     */
    use(middleware) {
        this.middleware.push(middleware);
        return this;
    }
    
    /**
     * Get available strategies
     */
    getStrategies() {
        return Array.from(this.strategies.entries()).map(([name, strategy]) => ({
            name,
            ...strategy.getMetadata()
        }));
    }
    
    /**
     * Get execution statistics
     */
    getStats() {
        return {
            ...this.stats,
            strategies: this.getStrategies()
        };
    }
    
    /**
     * Clear statistics
     */
    clearStats() {
        this.stats = {
            totalRuns: 0,
            successfulRuns: 0,
            failedRuns: 0,
            totalTokens: 0
        };
    }
    
    /**
     * Logging helper
     */
    log(message, level = 'info') {
        if (this.config.verbose || level === 'error') {
            log(`[AgentOrchestrator] ${message}`);
        }
    }
}

export default AgentOrchestrator;