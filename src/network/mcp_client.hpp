#pragma once

#include <atomic>
#include <thread>
#include <memory>
#include <functional>
#include <string>
#include <chrono>
#include <atomic_queue/atomic_queue.h>
#include <spdlog/spdlog.h>
#include <simdjson.h>
#include "../tcp_client.hpp"
#include "../include/types_core.hpp"

using namespace simdjson;

class MCPClient {
private:
  atomic_queue::AtomicQueueB2<AppEvent> event_queue_;
  std::unique_ptr<TcpClient> tcp_client_;
  std::thread event_processor_thread_;
  bool running_ = false;
  std::string host_;
  int port_;
  std::function<void(const std::string&, const std::string&)> response_callback_;

public:
  MCPClient(const std::string& host = "127.0.0.1", int port = 4000);
  ~MCPClient();
  
  bool Connect();
  bool IsConnected() const;
  void Stop();
  void SendRequest(const std::string& json);
  void SetResponseCallback(std::function<void(const std::string&, const std::string&)> callback);

private:
  void ProcessEvents();
  void HandleServerMessage(const std::string& message);
};