#pragma once

#include <atomic>
#include <thread>
#include <memory>
#include <functional>
#include <string>
#include <chrono>
#include <map>
#include <atomic_queue/atomic_queue.h>
#include <spdlog/spdlog.h>
#include <simdjson.h>
#include "../tcp_client.hpp"
#include "../include/types_core.hpp"
#include "../protocol/jsonrpc_messages.hpp"
#include "../protocol/jsonrpc_procedures.hpp"

using namespace simdjson;

class MCPClient {
private:
  atomic_queue::AtomicQueueB2<AppEvent> event_queue_;
  std::unique_ptr<TcpClient> tcp_client_;
  std::thread event_processor_thread_;
  bool running_ = false;
  std::string host_;
  int port_;
  
  // Legacy callback (kept for compatibility)
  std::function<void(const std::string&, const std::string&)> response_callback_;
  std::function<void(int64_t)> id_callback_;  // Callback for JSON-RPC response IDs
  
  // New procedure registry
  jsonrpc::ProcedureRegistry registry_;
  
  // Track pending requests for better error handling
  std::map<int64_t, std::string> pending_requests_;  // id -> method name

public:
  MCPClient(const std::string& host = "127.0.0.1", int port = 4000);
  ~MCPClient();
  
  bool Connect();
  bool IsConnected() const;
  void Stop();
  void SendRequest(const std::string& json);
  void SetResponseCallback(std::function<void(const std::string&, const std::string&)> callback);
  void SetIdCallback(std::function<void(int64_t)> callback) { id_callback_ = callback; }
  
  // New type-safe procedure interface with zero-overhead callbacks
  template<typename ParamsType, typename ResultType, typename SuccessCallback, typename ErrorCallback = std::nullptr_t>
  void Call(const jsonrpc::Procedure<ParamsType, ResultType>& proc,
            const ParamsType& params,
            SuccessCallback&& success_callback,
            ErrorCallback&& error_callback = nullptr) {
    
    // Generate request with random ID
    int64_t id = jsonrpc::GenerateRequestId();
    SPDLOG_INFO("Generated request ID: {} for method: {}", id, proc.name);
    
    // Register callbacks in the registry - wrap in std::function only when storing
    if constexpr (std::is_same_v<ErrorCallback, std::nullptr_t>) {
      registry_.RegisterRequest<ResultType>(id, proc.name, 
        std::function<void(const ResultType&)>(std::forward<SuccessCallback>(success_callback)),
        nullptr);
    } else {
      registry_.RegisterRequest<ResultType>(id, proc.name,
        std::function<void(const ResultType&)>(std::forward<SuccessCallback>(success_callback)),
        std::function<void(int, const std::string&)>(std::forward<ErrorCallback>(error_callback)));
    }
    SPDLOG_INFO("Registered request ID: {} in registry", id);
    
    // Track pending request
    pending_requests_[id] = proc.name;
    
    // Build and send request
    auto request = jsonrpc::JsonRpcRequestBuilder()
        .id(id)
        .method(proc.name)
        .params(params)
        .buildJson();
    
    SendRequest(request);
  }
  
  // Overload for procedures with no parameters (std::monostate)
  template<typename ResultType, typename SuccessCallback, typename ErrorCallback = std::nullptr_t>
  void Call(const jsonrpc::Procedure<std::monostate, ResultType>& proc,
            SuccessCallback&& success_callback,
            ErrorCallback&& error_callback = nullptr) {
    Call(proc, std::monostate{}, 
         std::forward<SuccessCallback>(success_callback), 
         std::forward<ErrorCallback>(error_callback));
  }
  
  // Set default error handler for all requests
  void SetDefaultErrorHandler(std::function<void(int, const std::string&)> handler) {
    registry_.SetDefaultErrorHandler(handler);
  }

private:
  void ProcessEvents();
  void HandleServerMessage(const std::string& message);
  void HandleJsonRpcMessage(const dom::element& doc);
  void HandleJsonRpcError(const dom::element& error, int64_t id);
  void HandleJsonRpcResult(const dom::element& result, const dom::element& method_elem, int64_t id);
  void HandleJsonRpcNotification(const std::string& method, const dom::element& params);
};