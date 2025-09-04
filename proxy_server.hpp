#ifndef PROXY_SERVER_HPP
#define PROXY_SERVER_HPP

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <netinet/in.h>
#include <chrono>
#include "lru_cache.hpp"

class ProxyServer {
public:
    ProxyServer(int port);
    ~ProxyServer();

    void start();
    void stop();

private:
    void acceptConnections();
    void handleClient(int client_socket);
    void proxyRequest(int client_socket, const std::string& request);
    bool forwardToOrigin(const std::string& host,
                         const std::string& path,
                         const std::string& method,
                         const std::string& version,
                         const std::string& original_request,
                         std::string& out_response);

    int server_socket;
    int port;
    std::atomic<bool> running;
    std::vector<std::thread> threads;
    std::mutex connection_mutex;
    std::condition_variable connection_cv;
    std::queue<int> connection_queue;

    // Cache and metrics
    LruCache cache{10 * 1024 * 1024}; // 10 MB capacity
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> cache_hits{0};
};

#endif // PROXY_SERVER_HPP
