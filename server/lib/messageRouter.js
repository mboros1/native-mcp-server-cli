/**
 * Message Router
 * Routes incoming messages to appropriate handlers
 */
class MessageRouter {
    constructor() {
        this.routes = new Map();
    }

    /**
     * Register a route handler
     * @param {string} type - Message type
     * @param {Function} handler - Async handler function
     */
    register(type, handler) {
        this.routes.set(type, handler);
    }

    /**
     * Register a conditional route
     * @param {Function} condition - Function that returns true if route should handle
     * @param {Function} handler - Async handler function
     */
    registerConditional(condition, handler) {
        // Store conditional routes separately
        if (!this.conditionalRoutes) {
            this.conditionalRoutes = [];
        }
        this.conditionalRoutes.push({ condition, handler });
    }

    /**
     * Route a message to the appropriate handler
     * @param {Object} payload - Message payload
     * @param {Object} context - Context object (socket, clientId, etc)
     * @returns {Promise<boolean>} - True if handled
     */
    async route(payload, context) {
        // Check direct type routes first
        if (this.routes.has(payload.type)) {
            const handler = this.routes.get(payload.type);
            await handler(payload, context);
            return true;
        }

        // Check conditional routes
        if (this.conditionalRoutes) {
            for (const route of this.conditionalRoutes) {
                if (route.condition(payload)) {
                    await route.handler(payload, context);
                    return true;
                }
            }
        }

        return false;
    }
}

export default MessageRouter;