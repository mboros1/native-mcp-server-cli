/**
 * Type definitions for MCP Bridge Server
 */

// ============= Core Types =============

export interface ChatMessage {
  role: 'user' | 'assistant' | 'system' | 'tool';
  content: string;
  tool_calls?: ToolCall[];
  tool_call_id?: string;
  name?: string;
}

export interface ToolCall {
  id: string;
  type: 'function';
  function: {
    name: string;
    arguments: string;
  };
}

export interface Tool {
  name: string;
  description: string;
  parameters: {
    type: 'object';
    properties: Record<string, any>;
    required?: string[];
  };
}

// ============= API Types =============

export interface ApiResponse {
  content: string;
  model: string;
  usage?: {
    prompt_tokens?: number;
    completion_tokens?: number;
    total_tokens?: number;
  };
  toolCalls?: ToolCall[] | null;
  finishReason?: 'stop' | 'tool_calls' | 'length' | 'error' | null;
}

export interface ApiConfig {
  id: string;
  name: string;
  provider: 'openai' | 'anthropic' | 'kimi' | 'o3';
  apiKey: string;
  baseURL?: string;
  models: ModelConfig[];
}

export interface ModelConfig {
  id: string;
  name: string;
  contextWindow: number;
  supportsTools: boolean;
  supportsVision?: boolean;
  temperature?: number;
}

// ============= Agent Types =============

export interface AgentState {
  // Core state
  conversationHistory: Array<{
    role: string;
    content: string;
    timestamp: number;
    iteration: number;
    [key: string]: any;
  }>;
  todoList: any[];
  currentPlan: any;
  
  // Execution tracking
  iterationCount: number;
  maxIterations: number;
  startTime: number;
  status: 'initialized' | 'planning' | 'executing' | 'completed' | 'failed';
  
  // Memory stores
  shortTermMemory: any[];
  workingMemory: Map<string, any>;
  
  // Checkpointing
  checkpoints: any[];
  lastCheckpoint: any;
  
  // Tool execution history
  toolCalls: ToolExecution[];
  observations: any[];
  
  // Error tracking
  errors: any[];
  retryCount: number;
  maxRetries: number;
  
  // Methods
  addMessage(role: string, content: string, metadata?: any): any;
  addToolCall(toolName: string, args: any, result: any, error?: string | null): any;
  addObservation(observation: any): any;
  checkpoint(label?: string): any;
  restore(checkpointId: number): any;
  nextIteration(): number;
  shouldStop(): boolean;
  getStats(): {
    iterations: number;
    toolCalls: number;
    observations: number;
    errors: number;
    duration: number;
    status: string;
    checkpoints: number;
  };
  getRecentObservations(n?: number): any[];
  getConversationContext(): ChatMessage[];
  remember(key: string, value: any): void;
  recall(key: string): any;
  clearMemory(): void;
  
  // For compatibility
  messages?: ChatMessage[];
  getMessages?(): ChatMessage[];
}

export interface ToolExecution {
  toolName: string;
  arguments: any;
  result: any;
  error?: string;
  timestamp: number;
}

export interface AgentContext {
  task: string;
  model: string;
  tools: Tool[];
  initialResponse?: ApiResponse;
  abortSignal?: AbortSignal;
}

export interface ThoughtResponse {
  content: string;
  model?: string;
  usage?: any;
  toolCalls?: ToolCall[] | null;
  finishReason?: 'stop' | 'tool_calls' | 'length' | 'error' | null;
  reasoning?: string;
  nextAction?: string;
}

// ============= JSON-RPC Types =============

export interface JsonRpcRequest {
  jsonrpc: '2.0';
  id: string | number;
  method: string;
  params?: any;
}

export interface JsonRpcResponse {
  jsonrpc: '2.0';
  id: string | number;
  result?: any;
  error?: JsonRpcError;
}

export interface JsonRpcError {
  code: number;
  message: string;
  data?: any;
}

export interface JsonRpcContext {
  clientId: string;
  socket: any;
  abortSignal?: AbortSignal;
}

// ============= Procedure Types =============

export interface ChatParams {
  content: string;
  model?: string;
  timeout?: number;
  reasoning_effort?: string;
}

export interface ChatResult {
  reply: string;
  model: string;
  usage?: any;
  timestamp?: number;
}

export interface ToolListResult {
  tools: string[];
  timestamp: number;
}

export interface ToolExecuteParams {
  tool_name: string;
  arguments: any;
}

export interface ToolExecuteResult {
  result: any;
  success: boolean;
  error?: string;
  timestamp: number;
}

// ============= MCP Types =============

export interface McpServer {
  name: string;
  command: string;
  args: string[];
  env?: Record<string, string>;
  enabled: boolean;
}

export interface McpTool {
  name: string;
  description: string;
  inputSchema: {
    type: 'object';
    properties: Record<string, any>;
    required?: string[];
  };
}

export interface McpServerConnection {
  server: McpServer;
  process: any;
  tools: McpTool[];
  connected: boolean;
  error?: string;
}