/**
 * @file tcp_client.hpp
 * @brief Asynchronous TCP client for server communication
 * @author Native MCP Team
 * @date 2025
 * 
 * This class provides a thread-safe, auto-reconnecting TCP client
 * that communicates with the MCP bridge server. It handles connection
 * management, message queuing, and event dispatching.
 */

#pragma once

#include <asio.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <functional>
#include <deque>
#include "event.hpp"
#include <atomic_queue.h>

/**
 * @class TcpClient
 * @brief Thread-safe TCP client with automatic reconnection
 * 
 * The TcpClient manages a persistent connection to the MCP bridge server,
 * automatically reconnecting on connection loss. It runs in a separate
 * thread and communicates with the main application through a lock-free
 * event queue.
 * 
 * Features:
 * - Automatic reconnection with exponential backoff
 * - Thread-safe message sending
 * - Non-blocking asynchronous I/O
 * - Event-based notification system
 * - Graceful shutdown
 * 
 * @note This class is thread-safe. All public methods can be called
 *       from any thread.
 * 
 * @example Basic usage
 * @code
 * atomic_queue::AtomicQueueB2<AppEvent> event_queue(1024);
 * TcpClient client(event_queue);
 * 
 * // Start connection to localhost:4000
 * client.start("127.0.0.1", 4000);
 * 
 * // Send a message (thread-safe)
 * if (client.is_connected()) {
 *     client.send_message("{\"type\":\"chat\",\"content\":\"Hello\"}");
 * }
 * 
 * // Process events from the queue
 * AppEvent event;
 * while (event_queue.try_pop(event)) {
 *     switch (event.type) {
 *         case EventType::Connected:
 *             // Handle connection established
 *             break;
 *         case EventType::MessageReceived:
 *             // Process received message in event.data
 *             break;
 *         case EventType::ConnectionLost:
 *             // Handle disconnection
 *             break;
 *     }
 * }
 * 
 * // Graceful shutdown
 * client.stop();
 * @endcode
 */
class TcpClient {
public:
    /**
     * @brief Construct a new TcpClient
     * @param event_queue Reference to the application event queue
     * 
     * The event queue is used to notify the application of connection
     * events and received messages without blocking.
     */
    TcpClient(atomic_queue::AtomicQueueB2<AppEvent>& event_queue)
        : event_queue_(event_queue),
          io_context_(),
          socket_(io_context_),
          reconnect_timer_(io_context_),
          connected_(false),
          running_(false) {}

    /**
     * @brief Destructor ensures clean shutdown
     * 
     * Automatically calls stop() if the client is still running
     */
    ~TcpClient() {
        stop();
    }

    /**
     * @brief Start the TCP client and connect to server
     * @param host Server hostname or IP address (default: "127.0.0.1")
     * @param port Server port number (default: 4000)
     * 
     * Starts the I/O thread and initiates connection. Connection events
     * will be delivered through the event queue.
     * 
     * @note This method is idempotent - calling it multiple times has no effect
     *       if already running.
     */
    void start(const std::string& host = "127.0.0.1", int port = 4000) {
        if (running_) return;
        
        running_ = true;
        host_ = host;
        port_ = port;
        
        worker_thread_ = std::thread([this]() {
            run();
        });
    }

    /**
     * @brief Stop the TCP client and close connection
     * 
     * Gracefully shuts down the connection and stops the I/O thread.
     * Blocks until the worker thread has terminated.
     * 
     * @note Thread-safe and idempotent
     */
    void stop() {
        if (!running_) return;
        
        running_ = false;
        reconnect_timer_.cancel();
        io_context_.stop();
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    /**
     * @brief Send a message to the server
     * @param message Message string to send (newline will be appended)
     * 
     * Messages are queued and sent asynchronously. If not connected,
     * the message is silently dropped.
     * 
     * @note Thread-safe - can be called from any thread
     * 
     * @example
     * @code
     * // Send JSON message
     * client.send_message("{\"type\":\"chat\",\"content\":\"Hello world\"}");
     * 
     * // Send command
     * client.send_message("{\"type\":\"reset\"}");
     * @endcode
     */
    void send_message(const std::string& message) {
        if (!connected_) return;
        
        asio::post(io_context_, [this, message]() {
            bool write_in_progress = !write_queue_.empty();
            write_queue_.push_back(message + "\n");
            
            if (!write_in_progress) {
                do_write();
            }
        });
    }

    /**
     * @brief Check if client is connected to server
     * @return true if connected, false otherwise
     * 
     * @note Thread-safe atomic read
     */
    bool is_connected() const {
        return connected_;
    }

private:
    /**
     * @brief Main I/O thread loop
     * 
     * Runs the ASIO event loop and handles automatic reconnection
     * on errors. Continues until stop() is called.
     */
    void run() {
        while (running_) {
            try {
                io_context_.restart();
                attempt_connection();
                io_context_.run();
            } catch (const std::exception& e) {
                connected_ = false;
                event_queue_.try_push(AppEvent{EventType::ConnectionLost, "Connection error: " + std::string(e.what())});
            }
            
            if (!running_) break;
        }
    }

    /**
     * @brief Attempt to connect to the server
     * 
     * Resolves the hostname and initiates asynchronous connection.
     * Posts connection events to the event queue.
     */
    void attempt_connection() {
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host_, std::to_string(port_));
        
        asio::async_connect(socket_, endpoints,
            [this](std::error_code ec, asio::ip::tcp::endpoint) {
                if (!ec) {
                    connected_ = true;
                    event_queue_.try_push(AppEvent{EventType::Connected, "Connected to server"});
                    do_read();
                } else {
                    event_queue_.try_push(AppEvent{EventType::ConnectionFailed, "Failed to connect: " + ec.message()});
                    schedule_reconnect();
                }
            });
    }
    
    /**
     * @brief Schedule reconnection attempt after delay
     * 
     * Waits 3 seconds before attempting to reconnect.
     * Cancelled if stop() is called.
     */
    void schedule_reconnect() {
        if (!running_) return;
        
        reconnect_timer_.expires_after(std::chrono::seconds(3));
        reconnect_timer_.async_wait([this](std::error_code ec) {
            if (!ec && running_) {
                socket_ = asio::ip::tcp::socket(io_context_);
                attempt_connection();
            }
        });
    }

    /**
     * @brief Read messages from socket
     * 
     * Reads newline-delimited messages and posts them to the event queue.
     * Automatically schedules reconnection on read errors.
     */
    void do_read() {
        asio::async_read_until(socket_, read_buffer_, '\n',
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    std::istream stream(&read_buffer_);
                    std::string line;
                    std::getline(stream, line);
                    
                    event_queue_.try_push(AppEvent{EventType::MessageReceived, line});
                    do_read();
                } else {
                    connected_ = false;
                    event_queue_.try_push(AppEvent{EventType::ConnectionLost, "Read error: " + ec.message()});
                    socket_.close();
                    schedule_reconnect();
                }
            });
    }

    /**
     * @brief Write queued messages to socket
     * 
     * Writes messages from the queue one at a time.
     * Handles write errors by closing connection and reconnecting.
     */
    void do_write() {
        asio::async_write(socket_, asio::buffer(write_queue_.front()),
            [this](std::error_code ec, std::size_t) {
                if (!ec) {
                    write_queue_.pop_front();
                    if (!write_queue_.empty()) {
                        do_write();
                    }
                } else {
                    connected_ = false;
                    event_queue_.try_push(AppEvent{EventType::ConnectionLost, "Write error: " + ec.message()});
                    socket_.close();
                    schedule_reconnect();
                }
            });
    }

    atomic_queue::AtomicQueueB2<AppEvent>& event_queue_; ///< Lock-free event queue for notifications
    asio::io_context io_context_;                        ///< ASIO I/O context for async operations
    asio::ip::tcp::socket socket_;                       ///< TCP socket
    asio::steady_timer reconnect_timer_;                 ///< Timer for reconnection delays
    asio::streambuf read_buffer_;                        ///< Buffer for incoming data
    std::deque<std::string> write_queue_;                ///< Queue of messages to send
    
    std::atomic<bool> connected_;                        ///< Connection status (thread-safe)
    std::atomic<bool> running_;                          ///< Running status (thread-safe)
    std::thread worker_thread_;                          ///< I/O worker thread
    
    std::string host_;                                   ///< Server hostname
    int port_;                                            ///< Server port
};