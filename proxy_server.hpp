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

    int server_socket;
    int port;
    std::atomic<bool> running;
    std::vector<std::thread> threads;
    std::mutex connection_mutex;
    std::condition_variable connection_cv;
    std::queue<int> connection_queue;
};

#endif // PROXY_SERVER_HPP
