/**
 * Todo Manager Component
 * 
 * Manages todo lists for agent task planning and tracking
 * Similar to Claude Code's todo system
 */

export class TodoManager {
    constructor(options = {}) {
        this.todos = [];
        this.idCounter = 0;
        this.maxTodos = options.maxTodos || 100;
        this.autoCleanup = options.autoCleanup !== false;
    }
    
    /**
     * Add a new todo item
     */
    add(content, options = {}) {
        const {
            dependencies = [],
            priority = 0,
            category = 'general',
            metadata = {}
        } = options;
        
        const todo = {
            id: String(++this.idCounter),
            content,
            status: 'pending',
            dependencies,
            priority,
            category,
            metadata,
            created: Date.now(),
            updated: Date.now(),
            attempts: 0,
            errors: []
        };
        
        this.todos.push(todo);
        
        // Auto cleanup if needed
        if (this.autoCleanup && this.todos.length > this.maxTodos) {
            this.cleanup();
        }
        
        return todo;
    }
    
    /**
     * Add multiple todos from a plan
     */
    addFromPlan(plan) {
        const todos = [];
        
        for (const item of plan) {
            if (typeof item === 'string') {
                todos.push(this.add(item));
            } else {
                todos.push(this.add(item.content, item));
            }
        }
        
        return todos;
    }
    
    /**
     * Get a todo by ID
     */
    get(id) {
        return this.todos.find(t => t.id === id);
    }
    
    /**
     * Update a todo's status
     */
    updateStatus(id, status) {
        const todo = this.get(id);
        if (!todo) {
            throw new Error(`Todo ${id} not found`);
        }
        
        const validStatuses = ['pending', 'in_progress', 'completed', 'failed', 'skipped'];
        if (!validStatuses.includes(status)) {
            throw new Error(`Invalid status: ${status}`);
        }
        
        todo.status = status;
        todo.updated = Date.now();
        
        // Track completion time
        if (status === 'completed' || status === 'failed') {
            todo.completedAt = Date.now();
            todo.duration = todo.completedAt - todo.created;
        }
        
        return todo;
    }
    
    /**
     * Mark a todo as in progress
     */
    markInProgress(id) {
        // First, ensure only one todo is in progress
        this.todos.forEach(t => {
            if (t.status === 'in_progress' && t.id !== id) {
                t.status = 'pending';
                t.updated = Date.now();
            }
        });
        
        return this.updateStatus(id, 'in_progress');
    }
    
    /**
     * Mark a todo as completed
     */
    markCompleted(id, result = null) {
        const todo = this.updateStatus(id, 'completed');
        if (result) {
            todo.result = result;
        }
        return todo;
    }
    
    /**
     * Mark a todo as failed
     */
    markFailed(id, error) {
        const todo = this.updateStatus(id, 'failed');
        todo.errors.push({
            error: error.message || error,
            timestamp: Date.now()
        });
        return todo;
    }
    
    /**
     * Retry a failed todo
     */
    retry(id) {
        const todo = this.get(id);
        if (!todo) {
            throw new Error(`Todo ${id} not found`);
        }
        
        todo.status = 'pending';
        todo.attempts++;
        todo.updated = Date.now();
        
        return todo;
    }
    
    /**
     * Get the next todo to work on
     */
    getNext() {
        // Find todos that are ready to execute
        const ready = this.todos
            .filter(t => t.status === 'pending')
            .filter(t => this.dependenciesMet(t))
            .sort((a, b) => {
                // Sort by priority first, then by creation time
                if (a.priority !== b.priority) {
                    return b.priority - a.priority;
                }
                return a.created - b.created;
            });
        
        return ready[0] || null;
    }
    
    /**
     * Check if a todo's dependencies are met
     */
    dependenciesMet(todo) {
        if (!todo.dependencies || todo.dependencies.length === 0) {
            return true;
        }
        
        for (const depId of todo.dependencies) {
            const dep = this.get(depId);
            if (!dep || dep.status !== 'completed') {
                return false;
            }
        }
        
        return true;
    }
    
    /**
     * Get todos by status
     */
    getByStatus(status) {
        return this.todos.filter(t => t.status === status);
    }
    
    /**
     * Get todos by category
     */
    getByCategory(category) {
        return this.todos.filter(t => t.category === category);
    }
    
    /**
     * Get current in-progress todo
     */
    getCurrent() {
        return this.todos.find(t => t.status === 'in_progress');
    }
    
    /**
     * Get all todos
     */
    getAll() {
        return [...this.todos];
    }
    
    /**
     * Get progress statistics
     */
    getProgress() {
        const total = this.todos.length;
        const completed = this.todos.filter(t => t.status === 'completed').length;
        const failed = this.todos.filter(t => t.status === 'failed').length;
        const inProgress = this.todos.filter(t => t.status === 'in_progress').length;
        const pending = this.todos.filter(t => t.status === 'pending').length;
        const skipped = this.todos.filter(t => t.status === 'skipped').length;
        
        return {
            total,
            completed,
            failed,
            inProgress,
            pending,
            skipped,
            percentComplete: total > 0 ? Math.round((completed / total) * 100) : 0,
            percentSuccess: (completed + failed) > 0 
                ? Math.round((completed / (completed + failed)) * 100) 
                : 0
        };
    }
    
    /**
     * Check if all todos are complete
     */
    isComplete() {
        return this.todos.every(t => 
            t.status === 'completed' || 
            t.status === 'skipped' ||
            t.status === 'failed'
        );
    }
    
    /**
     * Check if there are pending todos
     */
    hasPending() {
        return this.todos.some(t => t.status === 'pending');
    }
    
    /**
     * Clear completed todos
     */
    cleanup() {
        this.todos = this.todos.filter(t => 
            t.status !== 'completed' && 
            t.status !== 'skipped'
        );
    }
    
    /**
     * Clear all todos
     */
    clear() {
        this.todos = [];
        this.idCounter = 0;
    }
    
    /**
     * Export todos as formatted string
     */
    toString() {
        if (this.todos.length === 0) {
            return 'No todos';
        }
        
        const progress = this.getProgress();
        let output = `📋 Todo List (${progress.completed}/${progress.total} completed - ${progress.percentComplete}%)\n\n`;
        
        // Group by status
        const grouped = {
            in_progress: this.getByStatus('in_progress'),
            pending: this.getByStatus('pending'),
            completed: this.getByStatus('completed'),
            failed: this.getByStatus('failed')
        };
        
        if (grouped.in_progress.length > 0) {
            output += '🔄 In Progress:\n';
            grouped.in_progress.forEach(t => {
                output += `  [${t.id}] ${t.content}\n`;
            });
            output += '\n';
        }
        
        if (grouped.pending.length > 0) {
            output += '⏳ Pending:\n';
            grouped.pending.forEach(t => {
                output += `  [${t.id}] ${t.content}`;
                if (t.dependencies.length > 0) {
                    output += ` (depends on: ${t.dependencies.join(', ')})`;
                }
                output += '\n';
            });
            output += '\n';
        }
        
        if (grouped.completed.length > 0) {
            output += '✅ Completed:\n';
            grouped.completed.forEach(t => {
                output += `  [${t.id}] ${t.content}`;
                if (t.duration) {
                    output += ` (${Math.round(t.duration / 1000)}s)`;
                }
                output += '\n';
            });
            output += '\n';
        }
        
        if (grouped.failed.length > 0) {
            output += '❌ Failed:\n';
            grouped.failed.forEach(t => {
                output += `  [${t.id}] ${t.content}`;
                if (t.errors.length > 0) {
                    output += ` - ${t.errors[t.errors.length - 1].error}`;
                }
                output += '\n';
            });
        }
        
        return output;
    }
    
    /**
     * Export todos as JSON
     */
    toJSON() {
        return {
            todos: this.todos,
            progress: this.getProgress()
        };
    }
}

export default TodoManager;