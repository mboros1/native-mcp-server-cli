#pragma once

#include <asio.hpp>
#include <thread>
#include <atomic>
#include <memory>
#include <string>
#include <functional>
#include "event.hpp"
#include <atomic_queue.h>

class TcpClient {
public:
    TcpClient(atomic_queue::AtomicQueueB2<AppEvent>& event_queue)
        : event_queue_(event_queue),
          io_context_(),
          socket_(io_context_),
          reconnect_timer_(io_context_),
          connected_(false),
          running_(false) {}

    ~TcpClient() {
        stop();
    }

    void start(const std::string& host = "127.0.0.1", int port = 4000) {
        if (running_) return;
        
        running_ = true;
        host_ = host;
        port_ = port;
        
        worker_thread_ = std::thread([this]() {
            run();
        });
    }

    void stop() {
        if (!running_) return;
        
        running_ = false;
        reconnect_timer_.cancel();
        io_context_.stop();
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

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

    bool is_connected() const {
        return connected_;
    }

private:
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

    atomic_queue::AtomicQueueB2<AppEvent>& event_queue_;
    asio::io_context io_context_;
    asio::ip::tcp::socket socket_;
    asio::steady_timer reconnect_timer_;
    asio::streambuf read_buffer_;
    std::deque<std::string> write_queue_;
    
    std::atomic<bool> connected_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    
    std::string host_;
    int port_;
};