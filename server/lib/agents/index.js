/**
 * Agent System Entry Point
 * 
 * Exports all agent components for use in the application
 */

// Core components
export { AgentOrchestrator } from './core/AgentOrchestrator.js';
export { AgentState } from './core/AgentState.js';
export { AgentStrategy } from './core/AgentStrategy.js';

// Strategies
export { ReactAgent } from './strategies/ReactAgent.js';

// Components
export { TodoManager } from './components/TodoManager.js';

// Factory function to create a configured orchestrator
export function createAgentOrchestrator(options = {}) {
    const orchestrator = new AgentOrchestrator(options);
    
    // Register default strategies
    orchestrator.registerStrategy('react', new ReactAgent(options.react || {}));
    
    // Add more strategies as they're implemented
    // orchestrator.registerStrategy('plan-execute', new PlanExecuteAgent());
    // orchestrator.registerStrategy('multi-agent', new MultiAgentCoordinator());
    
    return orchestrator;
}

// No default export - use named exports only