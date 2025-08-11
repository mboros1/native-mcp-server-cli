#pragma once

#include <asio.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <queue>
#include <mutex>
#include <spdlog/spdlog.h>

/**
 * @brief Mock TCP server for testing MCP client connections
 * 
 * Provides a simple echo/response server that can be configured
 * to return specific responses for testing purposes.
 */
class MockTcpServer {
public:
    using ResponseCallback = std::function<std::string(const std::string&)>;
    
    MockTcpServer(unsigned short port = 0) 
        : acceptor_(io_context_),
          port_(port),
          running_(false) {
    }
    
    ~MockTcpServer() {
        Stop();
    }
    
    /**
     * @brief Start the server
     * @return The port number the server is listening on
     */
    unsigned short Start() {
        if (running_) return port_;
        
        // Bind to any available port if port is 0
        asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), port_);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();
        
        // Get the actual port if we bound to 0
        port_ = acceptor_.local_endpoint().port();
        
        running_ = true;
        
        // Start accepting connections
        StartAccept();
        
        // Run the IO context in a separate thread
        server_thread_ = std::thread([this]() {
            io_context_.run();
        });
        
        SPDLOG_INFO("Mock server started on port {}", port_);
        return port_;
    }
    
    /**
     * @brief Stop the server
     */
    void Stop() {
        if (!running_) return;
        
        running_ = false;
        io_context_.stop();
        
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        SPDLOG_INFO("Mock server stopped");
    }
    
    /**
     * @brief Set a callback to generate responses
     * @param callback Function that takes request and returns response
     */
    void SetResponseCallback(ResponseCallback callback) {
        response_callback_ = callback;
    }
    
    /**
     * @brief Queue a specific response to be sent
     * @param response The response to send
     */
    void QueueResponse(const std::string& response) {
        std::lock_guard<std::mutex> lock(response_mutex_);
        response_queue_.push(response);
    }
    
    /**
     * @brief Get the last received message
     * @return The last message received from a client
     */
    std::string GetLastReceivedMessage() const {
        std::lock_guard<std::mutex> lock(received_mutex_);
        return last_received_message_;
    }
    
    /**
     * @brief Get all received messages
     * @return Vector of all received messages
     */
    std::vector<std::string> GetReceivedMessages() const {
        std::lock_guard<std::mutex> lock(received_mutex_);
        return received_messages_;
    }
    
    /**
     * @brief Clear received messages
     */
    void ClearReceivedMessages() {
        std::lock_guard<std::mutex> lock(received_mutex_);
        received_messages_.clear();
        last_received_message_.clear();
    }
    
    /**
     * @brief Get the number of active connections
     */
    size_t GetConnectionCount() const {
        return connection_count_;
    }
    
    /**
     * @brief Wait for a connection with timeout
     * @param timeout_ms Timeout in milliseconds
     * @return true if connection established within timeout
     */
    bool WaitForConnection(int timeout_ms = 5000) {
        auto start = std::chrono::steady_clock::now();
        while (connection_count_ == 0) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > timeout_ms) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }
    
private:
    class Session : public std::enable_shared_from_this<Session> {
    public:
        Session(asio::ip::tcp::socket socket, MockTcpServer& server)
            : socket_(std::move(socket)), server_(server) {
            server_.connection_count_++;
            // Disable Nagle's algorithm for low latency
            socket_.set_option(asio::ip::tcp::no_delay(true));
        }
        
        ~Session() {
            server_.connection_count_--;
        }
        
        void Start() {
            DoRead();
        }
        
    private:
        void DoRead() {
            auto self(shared_from_this());
            socket_.async_read_some(
                asio::buffer(data_, max_length),
                [this, self](std::error_code ec, std::size_t length) {
                    if (!ec) {
                        std::string message(data_, length);
                        
                        // Store received message
                        {
                            std::lock_guard<std::mutex> lock(server_.received_mutex_);
                            server_.last_received_message_ = message;
                            server_.received_messages_.push_back(message);
                        }
                        
                        SPDLOG_DEBUG("Mock server received: {}", message);
                        
                        // Generate response
                        std::string response;
                        
                        // Check for queued responses first
                        {
                            std::lock_guard<std::mutex> lock(server_.response_mutex_);
                            if (!server_.response_queue_.empty()) {
                                response = server_.response_queue_.front();
                                server_.response_queue_.pop();
                            }
                        }
                        
                        // If no queued response, use callback
                        if (response.empty() && server_.response_callback_) {
                            response = server_.response_callback_(message);
                            SPDLOG_DEBUG("Response from callback: {}", response.substr(0, 200));
                        }
                        
                        // Default to echo if no response configured
                        if (response.empty()) {
                            response = "ECHO: " + message;
                            SPDLOG_DEBUG("Using default echo response");
                        }
                        
                        // CRITICAL: Add newline for framing (client expects newline-delimited JSON)
                        if (!response.empty() && response.back() != '\n') {
                            response.push_back('\n');
                        }
                        
                        SPDLOG_DEBUG("Sending response: {}", response.substr(0, 200));
                        
                        // Store response in a member to keep it alive during async write
                        write_buffer_ = std::move(response);
                        DoWrite();
                    }
                });
        }
        
        void DoWrite() {
            auto self(shared_from_this());
            asio::async_write(
                socket_,
                asio::buffer(write_buffer_),
                [this, self](std::error_code ec, std::size_t /*length*/) {
                    if (!ec) {
                        DoRead();
                    }
                });
        }
        
        asio::ip::tcp::socket socket_;
        MockTcpServer& server_;
        enum { max_length = 4096 };
        char data_[max_length];
        std::string write_buffer_;  // Keeps response alive during async write
    };
    
    void StartAccept() {
        acceptor_.async_accept(
            [this](std::error_code ec, asio::ip::tcp::socket socket) {
                if (!ec && running_) {
                    auto session = std::make_shared<Session>(std::move(socket), *this);
                    session->Start();
                }
                
                if (running_) {
                    StartAccept();
                }
            });
    }
    
    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::thread server_thread_;
    std::atomic<bool> running_;
    std::atomic<size_t> connection_count_{0};
    unsigned short port_;
    
    ResponseCallback response_callback_;
    std::queue<std::string> response_queue_;
    mutable std::mutex response_mutex_;
    
    std::string last_received_message_;
    std::vector<std::string> received_messages_;
    mutable std::mutex received_mutex_;
};