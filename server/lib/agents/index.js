/**
 * Agent System Entry Point
 * 
 * Exports all agent components for use in the application
 */

// Core components
import { AgentOrchestrator } from './core/AgentOrchestrator.js';
import { AgentState } from './core/AgentState.js';
import { AgentStrategy } from './core/AgentStrategy.js';

// Strategies
import { ReactAgent } from './strategies/ReactAgent.js';

// Components
import { TodoManager } from './components/TodoManager.js';

// Re-export for external use
export { AgentOrchestrator, AgentState, AgentStrategy };
export { ReactAgent };
export { TodoManager };

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