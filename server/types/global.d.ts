/**
 * Global type declarations
 * These types are available throughout the project without import
 */

declare global {
  namespace NodeJS {
    interface ProcessEnv {
      KIMI_API_KEY?: string;
      OPENAI_API_KEY?: string;
      PORT?: string;
      MCP_SERVER_PORT?: string;
      USE_AGENT_MODE?: string;
      DEBUG_AGENTS?: string;
      DEBUG_CONSOLE?: string;
      NODE_ENV?: 'development' | 'production' | 'test';
    }
  }
}

// Make this a module
export {};